#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <iconv.h>
#include <string>
#include <thread>

#include "ThostFtdcTraderApi.h"

struct TraderConfig {
    std::string front_address;
    std::string broker_id;
    std::string user_id;
    std::string password;
    std::string app_id;
    std::string auth_code;
    std::string user_product_info;
};

static std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n')) {
        ++start;
    }

    size_t end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n')) {
        --end;
    }
    return s.substr(start, end - start);
}

static bool parseIni(const std::string& path, std::map<std::string, std::map<std::string, std::string> >& data) {
    std::ifstream in(path.c_str());
    if (!in.is_open()) {
        return false;
    }

    std::string line;
    std::string current = "GLOBAL";
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }

        if (line.front() == '[' && line.back() == ']') {
            current = trim(line.substr(1, line.size() - 2));
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }

        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        data[current][key] = val;
    }

    return true;
}

static std::string getConfigValue(
    const std::map<std::string, std::map<std::string, std::string> >& data,
    const std::string& sec,
    const std::string& key) {
    std::map<std::string, std::map<std::string, std::string> >::const_iterator secIt = data.find(sec);
    if (secIt == data.end()) {
        return "";
    }
    std::map<std::string, std::string>::const_iterator keyIt = secIt->second.find(key);
    if (keyIt == secIt->second.end()) {
        return "";
    }
    return keyIt->second;
}

static bool loadTraderConfig(const std::string& path, TraderConfig& cfg) {
    std::map<std::string, std::map<std::string, std::string> > data;
    if (!parseIni(path, data)) {
        return false;
    }

    cfg.front_address = getConfigValue(data, "TRADER", "FrontAddress");
    cfg.broker_id = getConfigValue(data, "TRADER", "BrokerID");
    cfg.user_id = getConfigValue(data, "TRADER", "UserID");
    cfg.password = getConfigValue(data, "TRADER", "Password");
    cfg.app_id = getConfigValue(data, "TRADER", "AppID");
    cfg.auth_code = getConfigValue(data, "TRADER", "AuthCode");
    cfg.user_product_info = getConfigValue(data, "TRADER", "UserProductInfo");

    if (cfg.front_address.empty()) cfg.front_address = getConfigValue(data, "MD", "FrontAddress");
    if (cfg.broker_id.empty()) cfg.broker_id = getConfigValue(data, "MD", "BrokerID");
    if (cfg.user_id.empty()) cfg.user_id = getConfigValue(data, "MD", "UserID");
    if (cfg.password.empty()) cfg.password = getConfigValue(data, "MD", "Password");

    return !cfg.front_address.empty() && !cfg.broker_id.empty() && !cfg.user_id.empty() && !cfg.password.empty();
}

static std::string decodeErrorMsg(const char* msg) {
    if (!msg || msg[0] == '\0') {
        return "";
    }

    iconv_t cd = iconv_open("UTF-8", "GB18030");
    if (cd == reinterpret_cast<iconv_t>(-1)) {
        return msg;
    }

    size_t in_left = std::strlen(msg);
    size_t out_left = in_left * 4 + 1;
    std::string out(out_left, '\0');
    char* in_buf = const_cast<char*>(msg);
    char* out_buf = &out[0];

    const size_t rc = iconv(cd, &in_buf, &in_left, &out_buf, &out_left);
    iconv_close(cd);
    if (rc == static_cast<size_t>(-1)) {
        return msg;
    }

    out.resize(out.size() - out_left);
    return out;
}

class ForQuoteDemoSpi : public CThostFtdcTraderSpi {
public:
    ForQuoteDemoSpi(
        CThostFtdcTraderApi* api,
        const TraderConfig& cfg,
        const std::string& instrument_id,
        const std::string& exchange_id)
        : api_(api),
          cfg_(cfg),
          instrument_id_(instrument_id),
          exchange_id_(exchange_id) {}

    bool ready_for_forquote() const { return ready_for_forquote_.load(); }

    void triggerForQuote() {
        if (ready_for_forquote_.exchange(false)) {
            sendForQuoteInsert();
        }
    }

    void OnFrontConnected() override {
        std::cout << "[1/5] Front connected" << std::endl;
        if (cfg_.app_id.empty() || cfg_.auth_code.empty()) {
            std::cout << "AppID/AuthCode empty, skip authenticate and login directly." << std::endl;
            sendLogin();
            return;
        }
        sendAuthenticate();
    }

    void OnFrontDisconnected(int nReason) override {
        std::cerr << "Front disconnected, reason=" << nReason << std::endl;
    }

    void OnRspAuthenticate(
        CThostFtdcRspAuthenticateField*,
        CThostFtdcRspInfoField* pRspInfo,
        int,
        bool) override {
        if (hasError(pRspInfo, "[2/5] Authenticate failed")) {
            return;
        }

        std::cout << "[2/5] Authenticate success" << std::endl;
        sendLogin();
    }

    void OnRspUserLogin(
        CThostFtdcRspUserLoginField* pRspUserLogin,
        CThostFtdcRspInfoField* pRspInfo,
        int,
        bool) override {
        if (hasError(pRspInfo, "[3/5] Login failed")) {
            return;
        }

        front_id_ = pRspUserLogin ? pRspUserLogin->FrontID : 0;
        session_id_ = pRspUserLogin ? pRspUserLogin->SessionID : 0;
        max_order_ref_ = (pRspUserLogin && pRspUserLogin->MaxOrderRef[0] != '\0')
            ? std::atoi(pRspUserLogin->MaxOrderRef)
            : 0;

        std::cout << "[3/5] Login success, TradingDay="
                  << (pRspUserLogin ? pRspUserLogin->TradingDay : "")
                  << ", FrontID=" << front_id_
                  << ", SessionID=" << session_id_ << std::endl;
        sendSettlementInfoConfirm();
    }

    void OnRspSettlementInfoConfirm(
        CThostFtdcSettlementInfoConfirmField* pSettlementInfoConfirm,
        CThostFtdcRspInfoField* pRspInfo,
        int,
        bool) override {
        if (hasError(pRspInfo, "[4/5] Settlement confirm failed")) {
            return;
        }

        std::cout << "[4/5] Settlement confirmed"
                  << ", ConfirmDate=" << (pSettlementInfoConfirm ? pSettlementInfoConfirm->ConfirmDate : "")
                  << ", ConfirmTime=" << (pSettlementInfoConfirm ? pSettlementInfoConfirm->ConfirmTime : "")
                  << std::endl;
        ready_for_forquote_ = true;
        std::cout << "Settlement confirmed. Press Enter to send ForQuote request." << std::endl;
    }

    void OnRspForQuoteInsert(
        CThostFtdcInputForQuoteField* pInputForQuote,
        CThostFtdcRspInfoField* pRspInfo,
        int,
        bool) override {
        if (hasError(pRspInfo, "[5/5] ForQuote insert failed")) {
            return;
        }

        std::cout << "[5/5] ForQuote request accepted"
                  << ", InstrumentID=" << extractInstrumentID(pInputForQuote)
                  << ", ExchangeID=" << (pInputForQuote ? pInputForQuote->ExchangeID : "")
                  << ", ForQuoteRef=" << (pInputForQuote ? pInputForQuote->ForQuoteRef : "")
                  << std::endl;
    }

    void OnErrRtnForQuoteInsert(
        CThostFtdcInputForQuoteField* pInputForQuote,
        CThostFtdcRspInfoField* pRspInfo) override {
        std::cerr << "OnErrRtnForQuoteInsert"
                  << ", InstrumentID=" << extractInstrumentID(pInputForQuote)
                  << ", ExchangeID=" << (pInputForQuote ? pInputForQuote->ExchangeID : "")
                  << ", ErrorID=" << (pRspInfo ? pRspInfo->ErrorID : -1)
                  << ", ErrorMsg=" << (pRspInfo ? decodeErrorMsg(pRspInfo->ErrorMsg) : "") << std::endl;
    }

    void OnRtnForQuoteRsp(CThostFtdcForQuoteRspField* pForQuoteRsp) override {
        std::cout << "OnRtnForQuoteRsp"
                  << ", InstrumentID=" << (pForQuoteRsp ? pForQuoteRsp->InstrumentID : "")
                  << ", ExchangeID=" << (pForQuoteRsp ? pForQuoteRsp->ExchangeID : "")
                  << ", ForQuoteSysID=" << (pForQuoteRsp ? pForQuoteRsp->ForQuoteSysID : "")
                  << ", ForQuoteTime=" << (pForQuoteRsp ? pForQuoteRsp->ForQuoteTime : "")
                  << std::endl;
    }

    void OnRspError(CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool) override {
        if (!pRspInfo || pRspInfo->ErrorID == 0) {
            return;
        }
        std::cerr << "OnRspError req=" << nRequestID
                  << ", ErrorID=" << pRspInfo->ErrorID
                  << ", ErrorMsg=" << decodeErrorMsg(pRspInfo->ErrorMsg) << std::endl;
    }

private:
    const char* extractInstrumentID(CThostFtdcInputForQuoteField* pInputForQuote) const {
        if (!pInputForQuote) {
            return "";
        }
        return pInputForQuote->InstrumentID[0] != '\0'
            ? pInputForQuote->InstrumentID
            : pInputForQuote->reserve1;
    }

    bool hasError(CThostFtdcRspInfoField* pRspInfo, const char* stage) {
        if (!pRspInfo || pRspInfo->ErrorID == 0) {
            return false;
        }
        std::cerr << stage
                  << ", ErrorID=" << pRspInfo->ErrorID
                  << ", ErrorMsg=" << decodeErrorMsg(pRspInfo->ErrorMsg) << std::endl;
        return true;
    }

    void sendAuthenticate() {
        CThostFtdcReqAuthenticateField req;
        std::memset(&req, 0, sizeof(req));
        std::strncpy(req.BrokerID, cfg_.broker_id.c_str(), sizeof(req.BrokerID) - 1);
        std::strncpy(req.UserID, cfg_.user_id.c_str(), sizeof(req.UserID) - 1);
        std::strncpy(req.AppID, cfg_.app_id.c_str(), sizeof(req.AppID) - 1);
        std::strncpy(req.AuthCode, cfg_.auth_code.c_str(), sizeof(req.AuthCode) - 1);
        if (!cfg_.user_product_info.empty()) {
            std::strncpy(req.UserProductInfo, cfg_.user_product_info.c_str(), sizeof(req.UserProductInfo) - 1);
        }

        const int rc = api_->ReqAuthenticate(&req, ++request_id_);
        if (rc != 0) {
            std::cerr << "ReqAuthenticate failed, ret=" << rc << std::endl;
            return;
        }
        std::cout << "Authenticate request sent." << std::endl;
    }

    void sendLogin() {
        CThostFtdcReqUserLoginField req;
        std::memset(&req, 0, sizeof(req));
        std::strncpy(req.BrokerID, cfg_.broker_id.c_str(), sizeof(req.BrokerID) - 1);
        std::strncpy(req.UserID, cfg_.user_id.c_str(), sizeof(req.UserID) - 1);
        std::strncpy(req.Password, cfg_.password.c_str(), sizeof(req.Password) - 1);

        const int rc = api_->ReqUserLogin(&req, ++request_id_);
        if (rc != 0) {
            std::cerr << "ReqUserLogin failed, ret=" << rc << std::endl;
            return;
        }
        std::cout << "Login request sent." << std::endl;
    }

    void sendSettlementInfoConfirm() {
        CThostFtdcSettlementInfoConfirmField req;
        std::memset(&req, 0, sizeof(req));
        std::strncpy(req.BrokerID, cfg_.broker_id.c_str(), sizeof(req.BrokerID) - 1);
        std::strncpy(req.InvestorID, cfg_.user_id.c_str(), sizeof(req.InvestorID) - 1);

        const int rc = api_->ReqSettlementInfoConfirm(&req, ++request_id_);
        if (rc != 0) {
            std::cerr << "ReqSettlementInfoConfirm failed, ret=" << rc << std::endl;
            return;
        }
        std::cout << "Settlement confirm request sent." << std::endl;
    }

    void sendForQuoteInsert() {
        CThostFtdcInputForQuoteField req;
        std::memset(&req, 0, sizeof(req));
        std::strncpy(req.BrokerID, cfg_.broker_id.c_str(), sizeof(req.BrokerID) - 1);
        std::strncpy(req.InvestorID, cfg_.user_id.c_str(), sizeof(req.InvestorID) - 1);
        std::strncpy(req.UserID, cfg_.user_id.c_str(), sizeof(req.UserID) - 1);
        std::strncpy(req.InstrumentID, instrument_id_.c_str(), sizeof(req.InstrumentID) - 1);
        std::strncpy(req.ExchangeID, exchange_id_.c_str(), sizeof(req.ExchangeID) - 1);

        ++max_order_ref_;
        std::snprintf(req.ForQuoteRef, sizeof(req.ForQuoteRef), "%d", max_order_ref_);

        const int rc = api_->ReqForQuoteInsert(&req, ++request_id_);
        if (rc != 0) {
            std::cerr << "ReqForQuoteInsert failed, ret=" << rc << std::endl;
            return;
        }

        std::cout << "[5/5] ForQuote request sent"
                  << ", InstrumentID=" << instrument_id_
                  << ", ExchangeID=" << exchange_id_
                  << ", ForQuoteRef=" << req.ForQuoteRef << std::endl;
    }

private:
    CThostFtdcTraderApi* api_;
    TraderConfig cfg_;
    std::string instrument_id_;
    std::string exchange_id_;
    int request_id_ = 0;
    int front_id_ = 0;
    int session_id_ = 0;
    int max_order_ref_ = 0;
    std::atomic<bool> ready_for_forquote_{false};
};

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <instrument_id> <exchange_id> [config_path]" << std::endl;
        std::cerr << "Example: " << argv[0] << " au2412 SHFE config/config.ini" << std::endl;
        return 1;
    }

    const std::string instrument_id = argv[1];
    const std::string exchange_id = argv[2];
    std::string config_path = "config/config.ini";
    if (argc > 3) {
        config_path = argv[3];
    }

    TraderConfig cfg;
    if (!loadTraderConfig(config_path, cfg)) {
        std::cerr << "Load config failed: " << config_path << std::endl;
        std::cerr << "Required fields: FrontAddress/BrokerID/UserID/Password in [TRADER] or [MD]" << std::endl;
        return 1;
    }

    std::cout << "ForQuote demo config:" << std::endl;
    std::cout << "  FrontAddress=" << cfg.front_address << std::endl;
    std::cout << "  BrokerID=" << cfg.broker_id << std::endl;
    std::cout << "  UserID=" << cfg.user_id << std::endl;
    std::cout << "  InstrumentID=" << instrument_id << std::endl;
    std::cout << "  ExchangeID=" << exchange_id << std::endl;
    std::cout << "  ApiVersion=" << CThostFtdcTraderApi::GetApiVersion() << std::endl;

    CThostFtdcTraderApi* api = CThostFtdcTraderApi::CreateFtdcTraderApi("flow_for_quote/");
    if (!api) {
        std::cerr << "CreateFtdcTraderApi failed." << std::endl;
        return 1;
    }

    ForQuoteDemoSpi spi(api, cfg, instrument_id, exchange_id);
    api->RegisterSpi(&spi);
    api->RegisterFront(const_cast<char*>(cfg.front_address.c_str()));
    api->Init();

    while (!spi.ready_for_forquote()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::string ignored;
    std::getline(std::cin, ignored);
    spi.triggerForQuote();

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
