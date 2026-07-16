#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iconv.h>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "ThostFtdcTraderApi.h"

struct TraderConfig {
    std::string front_address;
    std::string broker_id;
    std::string user_id;
    std::string password;
    std::string app_id;
    std::string auth_code;
    std::string user_product_info;
    std::vector<std::string> instruments;
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

static std::string decodeGbkToUtf8(const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return "";
    }

    std::string input(text);
    iconv_t cd = iconv_open("UTF-8", "GB18030");
    if (cd == reinterpret_cast<iconv_t>(-1)) {
        return input;
    }

    size_t in_left = input.size();
    size_t out_left = input.size() * 4 + 1;
    std::string output(out_left, '\0');
    char* in_buf = const_cast<char*>(input.data());
    char* out_buf = &output[0];

    if (iconv(cd, &in_buf, &in_left, &out_buf, &out_left) == static_cast<size_t>(-1)) {
        iconv_close(cd);
        return input;
    }

    iconv_close(cd);
    output.resize(output.size() - out_left);
    return output;
}

static void printRspError(const char* stage, CThostFtdcRspInfoField* pRspInfo) {
    if (!pRspInfo || pRspInfo->ErrorID == 0) {
        return;
    }

    std::cerr << stage
              << ", ErrorID=" << pRspInfo->ErrorID
              << ", ErrorMsg=" << decodeGbkToUtf8(pRspInfo->ErrorMsg) << std::endl;
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
    cfg.instruments.clear();

    const std::string instruments = getConfigValue(data, "INSTRUMENTS", "Instruments");
    for (size_t i = 0; i < instruments.size(); ) {
        size_t j = instruments.find(',', i);
        if (j == std::string::npos) {
            j = instruments.size();
        }
        const std::string instrument = trim(instruments.substr(i, j - i));
        if (!instrument.empty()) {
            cfg.instruments.push_back(instrument);
        }
        i = j + 1;
    }

    if (cfg.front_address.empty()) cfg.front_address = getConfigValue(data, "MD", "FrontAddress");
    if (cfg.broker_id.empty()) cfg.broker_id = getConfigValue(data, "MD", "BrokerID");
    if (cfg.user_id.empty()) cfg.user_id = getConfigValue(data, "MD", "UserID");
    if (cfg.password.empty()) cfg.password = getConfigValue(data, "MD", "Password");

    return !cfg.front_address.empty() && !cfg.broker_id.empty() && !cfg.user_id.empty() &&
           !cfg.password.empty() && !cfg.instruments.empty();
}

static bool isValidPrice(double price) {
    return std::isfinite(price) && price > 0.0 && price < 1e20;
}

static std::string escapeCsv(const std::string& value) {
    if (value.find_first_of(",\"\n\r") == std::string::npos) {
        return value;
    }

    std::string escaped = "\"";
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '"') {
            escaped += "\"\"";
        } else {
            escaped += value[i];
        }
    }
    escaped += "\"";
    return escaped;
}

static std::string priceToString(double price) {
    std::ostringstream oss;
    oss << price;
    return oss.str();
}

class TradeLatestPriceSpi : public CThostFtdcTraderSpi {
public:
    TradeLatestPriceSpi(
        CThostFtdcTraderApi* api,
        const TraderConfig& cfg)
        : api_(api), cfg_(cfg), csv_path_("trade_latest_price_results.csv") {
        csv_file_.open(csv_path_.c_str(), std::ios::out | std::ios::trunc);
        if (!csv_file_.is_open()) {
            std::cerr << "Open CSV failed: " << csv_path_ << std::endl;
        } else {
            csv_file_ << "query_instrument,instrument_id,trading_day,action_day,update_time,update_millisec,"
                      << "last_price,has_last_price,settlement_price,has_settlement_price,status,error_msg\n";
        }
    }

    bool finished() const { return finished_.load(); }
    bool success() const { return success_.load(); }
    const std::string& csv_path() const { return csv_path_; }

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
        finished_ = true;
        success_ = false;
    }

    void OnRspAuthenticate(
        CThostFtdcRspAuthenticateField*,
        CThostFtdcRspInfoField* pRspInfo,
        int,
        bool) override {
        if (pRspInfo && pRspInfo->ErrorID != 0) {
            printRspError("[2/5] Authenticate failed", pRspInfo);
            finished_ = true;
            success_ = false;
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
        if (pRspInfo && pRspInfo->ErrorID != 0) {
            printRspError("[3/5] Login failed", pRspInfo);
            finished_ = true;
            success_ = false;
            return;
        }

        std::cout << "[3/5] Login success, TradingDay="
                  << (pRspUserLogin ? pRspUserLogin->TradingDay : "") << std::endl;
        sendSettlementInfoConfirm();
    }

    void OnRspSettlementInfoConfirm(
        CThostFtdcSettlementInfoConfirmField* pSettlementInfoConfirm,
        CThostFtdcRspInfoField* pRspInfo,
        int,
        bool) override {
        if (pRspInfo && pRspInfo->ErrorID != 0) {
            printRspError("[4/5] Settlement confirm failed", pRspInfo);
            finished_ = true;
            success_ = false;
            return;
        }

        std::cout << "[4/5] Settlement confirmed"
                  << ", ConfirmDate=" << (pSettlementInfoConfirm ? pSettlementInfoConfirm->ConfirmDate : "")
                  << ", ConfirmTime=" << (pSettlementInfoConfirm ? pSettlementInfoConfirm->ConfirmTime : "")
                  << std::endl;
        sendNextQryDepthMarketData();
    }

    void OnRspQryDepthMarketData(
        CThostFtdcDepthMarketDataField* pDepthMarketData,
        CThostFtdcRspInfoField* pRspInfo,
        int,
        bool bIsLast) override {
        const std::string& instrument_id = cfg_.instruments[current_index_];

        if (pRspInfo && pRspInfo->ErrorID != 0) {
            printRspError("[5/5] Query depth market data failed", pRspInfo);
            writeCsvRow(
                instrument_id, instrument_id, "", "", "", 0, 0.0, false, 0.0, false,
                "ERROR", decodeGbkToUtf8(pRspInfo->ErrorMsg));
            finishCurrentQuery(false);
            return;
        }

        if (pDepthMarketData) {
            const bool has_last_price = isValidPrice(pDepthMarketData->LastPrice);
            const bool has_settlement_price = isValidPrice(pDepthMarketData->SettlementPrice);
            current_query_has_result_ = true;

            std::cout << "[5/5] Depth market data received"
                      << " (" << (current_index_ + 1) << "/" << cfg_.instruments.size() << ")" << std::endl;
            std::cout << "  QueryInstrument=" << instrument_id << std::endl;
            std::cout << "  InstrumentID=" << pDepthMarketData->InstrumentID << std::endl;
            std::cout << "  TradingDay=" << pDepthMarketData->TradingDay << std::endl;
            std::cout << "  ActionDay=" << pDepthMarketData->ActionDay << std::endl;
            std::cout << "  UpdateTime=" << pDepthMarketData->UpdateTime
                      << "." << pDepthMarketData->UpdateMillisec << std::endl;
            std::cout << "  LastPrice=" << pDepthMarketData->LastPrice
                      << (has_last_price ? " (valid)" : " (invalid)") << std::endl;
            std::cout << "  SettlementPrice=" << pDepthMarketData->SettlementPrice
                      << (has_settlement_price ? " (valid)" : " (invalid)") << std::endl;
            std::cout << "  HasSettlementPrice=" << (has_settlement_price ? "YES" : "NO") << std::endl;
            writeCsvRow(
                instrument_id,
                pDepthMarketData->InstrumentID,
                pDepthMarketData->TradingDay,
                pDepthMarketData->ActionDay,
                pDepthMarketData->UpdateTime,
                pDepthMarketData->UpdateMillisec,
                pDepthMarketData->LastPrice,
                has_last_price,
                pDepthMarketData->SettlementPrice,
                has_settlement_price,
                "OK",
                "");
        }

        if (bIsLast) {
            if (current_query_has_result_) {
                finishCurrentQuery(true);
            } else {
                std::cerr << "No depth market data returned for InstrumentID="
                          << instrument_id << std::endl;
                writeCsvRow(
                    instrument_id, instrument_id, "", "", "", 0, 0.0, false, 0.0, false,
                    "NO_DATA", "");
                finishCurrentQuery(false);
            }
        }
    }

    void OnRspError(CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool) override {
        if (!pRspInfo || pRspInfo->ErrorID == 0) {
            return;
        }
        std::string stage = "OnRspError req=" + std::to_string(nRequestID);
        printRspError(stage.c_str(), pRspInfo);
    }

private:
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
            finished_ = true;
            success_ = false;
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
            finished_ = true;
            success_ = false;
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
            finished_ = true;
            success_ = false;
            return;
        }
        std::cout << "Settlement confirm request sent." << std::endl;
    }

    void sendNextQryDepthMarketData() {
        if (current_index_ >= cfg_.instruments.size()) {
            finished_ = true;
            success_ = (failed_queries_ == 0 && successful_queries_ > 0);
            std::cout << "Query summary: success=" << successful_queries_
                      << ", failed=" << failed_queries_ << std::endl;
            if (csv_file_.is_open()) {
                std::cout << "CSV output: " << csv_path_ << std::endl;
            }
            return;
        }

        current_query_has_result_ = false;
        const std::string& instrument_id = cfg_.instruments[current_index_];
        CThostFtdcQryDepthMarketDataField req;
        std::memset(&req, 0, sizeof(req));
        std::strncpy(req.InstrumentID, instrument_id.c_str(), sizeof(req.InstrumentID) - 1);

        const int rc = api_->ReqQryDepthMarketData(&req, ++request_id_);
        if (rc != 0) {
            std::cerr << "ReqQryDepthMarketData failed, ret=" << rc
                      << ", InstrumentID=" << instrument_id << std::endl;
            writeCsvRow(
                instrument_id, instrument_id, "", "", "", 0, 0.0, false, 0.0, false,
                "REQ_ERROR", "ReqQryDepthMarketData ret=" + std::to_string(rc));
            finishCurrentQuery(false);
            return;
        }

        std::cout << "Depth market data query sent"
                  << " (" << (current_index_ + 1) << "/" << cfg_.instruments.size() << ")"
                  << ", InstrumentID=" << instrument_id << std::endl;
    }

    void finishCurrentQuery(bool success) {
        if (success) {
            ++successful_queries_;
        } else {
            ++failed_queries_;
        }
        std::this_thread::sleep_for(std::chrono::seconds(3));
        ++current_index_;
        sendNextQryDepthMarketData();
    }

    void writeCsvRow(
        const std::string& query_instrument,
        const std::string& instrument_id,
        const std::string& trading_day,
        const std::string& action_day,
        const std::string& update_time,
        int update_millisec,
        double last_price,
        bool has_last_price,
        double settlement_price,
        bool has_settlement_price,
        const std::string& status,
        const std::string& error_msg) {
        if (!csv_file_.is_open()) {
            return;
        }

        csv_file_ << escapeCsv(query_instrument) << ","
                  << escapeCsv(instrument_id) << ","
                  << escapeCsv(trading_day) << ","
                  << escapeCsv(action_day) << ","
                  << escapeCsv(update_time) << ","
                  << update_millisec << ","
                  << (has_last_price ? priceToString(last_price) : "") << ","
                  << (has_last_price ? "YES" : "NO") << ","
                  << (has_settlement_price ? priceToString(settlement_price) : "") << ","
                  << (has_settlement_price ? "YES" : "NO") << ","
                  << escapeCsv(status) << ","
                  << escapeCsv(error_msg) << "\n";
        csv_file_.flush();
    }

private:
    CThostFtdcTraderApi* api_;
    TraderConfig cfg_;
    std::ofstream csv_file_;
    std::string csv_path_;
    int request_id_ = 0;
    size_t current_index_ = 0;
    int successful_queries_ = 0;
    int failed_queries_ = 0;
    bool current_query_has_result_ = false;
    std::atomic<bool> finished_{false};
    std::atomic<bool> success_{false};
};

int main(int argc, char* argv[]) {
    std::string config_path = "config/config.ini";
    if (argc > 1) {
        config_path = argv[1];
    }

    TraderConfig cfg;
    if (!loadTraderConfig(config_path, cfg)) {
        std::cerr << "Load config failed: " << config_path << std::endl;
        std::cerr << "Required fields: FrontAddress/BrokerID/UserID/Password in [TRADER] or [MD], "
                  << "and [INSTRUMENTS] Instruments=..." << std::endl;
        return 1;
    }

    std::cout << "Trade latest price config:" << std::endl;
    std::cout << "  FrontAddress=" << cfg.front_address << std::endl;
    std::cout << "  BrokerID=" << cfg.broker_id << std::endl;
    std::cout << "  UserID=" << cfg.user_id << std::endl;
    std::cout << "  Instruments(" << cfg.instruments.size() << ")=";
    for (size_t i = 0; i < cfg.instruments.size(); ++i) {
        if (i != 0) std::cout << ",";
        std::cout << cfg.instruments[i];
    }
    std::cout << std::endl;
    std::cout << "  ApiVersion=" << CThostFtdcTraderApi::GetApiVersion() << std::endl;

    CThostFtdcTraderApi* api = CThostFtdcTraderApi::CreateFtdcTraderApi("flow_trade_latest_price/");
    if (!api) {
        std::cerr << "CreateFtdcTraderApi failed." << std::endl;
        return 1;
    }

    TradeLatestPriceSpi spi(api, cfg);
    api->RegisterSpi(&spi);
    api->RegisterFront(const_cast<char*>(cfg.front_address.c_str()));
    api->Init();

    const int timeout_sec = 120;
    for (int i = 0; i < timeout_sec * 10; ++i) {
        if (spi.finished()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    const bool timeout = !spi.finished();
    const bool ok = spi.success() && !timeout;
    if (timeout) {
        std::cerr << "Query timeout (" << timeout_sec << "s)." << std::endl;
    }

    api->RegisterSpi(nullptr);
    api->Release();

    std::cout << (ok ? "TRADE_LATEST_PRICE_PASS" : "TRADE_LATEST_PRICE_FAIL") << std::endl;
    return ok ? 0 : 2;
}
