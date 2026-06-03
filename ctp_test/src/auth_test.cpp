#include <atomic>
#include <chrono>
#include <cstring>
#include <iconv.h>
#include <fstream>
#include <iostream>
#include <map>
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

    const std::string decoded = decodeGbkToUtf8(pRspInfo->ErrorMsg);
    std::cerr << stage << ", ErrorID=" << pRspInfo->ErrorID
              << ", ErrorMsgRaw=" << pRspInfo->ErrorMsg;
    if (!decoded.empty() && decoded != pRspInfo->ErrorMsg) {
        std::cerr << ", ErrorMsgUtf8=" << decoded;
    }
    std::cerr << std::endl;
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

    // Backward-compatible fallback to [MD].
    if (cfg.front_address.empty()) cfg.front_address = getConfigValue(data, "MD", "FrontAddress");
    if (cfg.broker_id.empty()) cfg.broker_id = getConfigValue(data, "MD", "BrokerID");
    if (cfg.user_id.empty()) cfg.user_id = getConfigValue(data, "MD", "UserID");
    if (cfg.password.empty()) cfg.password = getConfigValue(data, "MD", "Password");

    return !cfg.front_address.empty() && !cfg.broker_id.empty() && !cfg.user_id.empty() && !cfg.password.empty();
}

class AuthTestSpi : public CThostFtdcTraderSpi {
public:
    explicit AuthTestSpi(CThostFtdcTraderApi* api, const TraderConfig& cfg)
        : api_(api), cfg_(cfg), finished_(false), success_(false) {}

    bool finished() const { return finished_.load(); }
    bool success() const { return success_.load(); }

    void OnFrontConnected() override {
        std::cout << "[1/3] Front connected" << std::endl;
        if (cfg_.app_id.empty() || cfg_.auth_code.empty()) {
            std::cout << "AppID/AuthCode empty, skip authenticate and login directly." << std::endl;
            sendLogin();
            return;
        }
        sendAuthenticate();
    }

    void OnFrontDisconnected(int nReason) override {
        std::cerr << "Front disconnected, reason=" << nReason << std::endl;
        if (!finished_) {
            finished_ = true;
            success_ = false;
        }
    }

    void OnRspAuthenticate(
        CThostFtdcRspAuthenticateField*,
        CThostFtdcRspInfoField* pRspInfo,
        int,
        bool) override {
        if (pRspInfo && pRspInfo->ErrorID != 0) {
            printRspError("[2/3] Authenticate failed", pRspInfo);
            finished_ = true;
            success_ = false;
            return;
        }

        std::cout << "[2/3] Authenticate success" << std::endl;
        sendLogin();
    }

    void OnRspUserLogin(
        CThostFtdcRspUserLoginField* pRspUserLogin,
        CThostFtdcRspInfoField* pRspInfo,
        int,
        bool) override {
        if (pRspInfo && pRspInfo->ErrorID != 0) {
            printRspError("[3/3] Login failed", pRspInfo);
            finished_ = true;
            success_ = false;
            return;
        }

        std::cout << "[3/3] Login success, TradingDay="
                  << (pRspUserLogin ? pRspUserLogin->TradingDay : "") << std::endl;
        finished_ = true;
        success_ = true;
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
        std::cout << "Authenticate request sent:"
                  << " BrokerID=" << cfg_.broker_id
                  << " UserID=" << cfg_.user_id
                  << " AppID=" << cfg_.app_id
                  << " AuthCode=" << (cfg_.auth_code.empty() ? "(empty)" : "(set)")
                  << " UserProductInfo=" << (cfg_.user_product_info.empty() ? "(empty)" : cfg_.user_product_info)
                  << std::endl;
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

private:
    CThostFtdcTraderApi* api_;
    TraderConfig cfg_;
    int request_id_ = 0;
    std::atomic<bool> finished_;
    std::atomic<bool> success_;
};

int main(int argc, char* argv[]) {
    std::string config_path = "config/config.ini";
    if (argc > 1) {
        config_path = argv[1];
    }

    TraderConfig cfg;
    if (!loadTraderConfig(config_path, cfg)) {
        std::cerr << "Load config failed: " << config_path << std::endl;
        std::cerr << "Required fields: FrontAddress/BrokerID/UserID/Password in [TRADER] or [MD]" << std::endl;
        return 1;
    }

    std::cout << "Auth test config:" << std::endl;
    std::cout << "  FrontAddress=" << cfg.front_address << std::endl;
    std::cout << "  BrokerID=" << cfg.broker_id << std::endl;
    std::cout << "  UserID=" << cfg.user_id << std::endl;
    std::cout << "  AppID=" << cfg.app_id << std::endl;
    std::cout << "  AuthCode=" << (cfg.auth_code.empty() ? "(empty)" : "(set)") << std::endl;

    CThostFtdcTraderApi* api = CThostFtdcTraderApi::CreateFtdcTraderApi("flow_auth_test/");
    if (!api) {
        std::cerr << "CreateFtdcTraderApi failed." << std::endl;
        return 1;
    }

    AuthTestSpi spi(api, cfg);
    api->RegisterSpi(&spi);
    api->RegisterFront(const_cast<char*>(cfg.front_address.c_str()));
    api->Init();

    const int timeout_sec = 20;
    for (int i = 0; i < timeout_sec * 10; ++i) {
        if (spi.finished()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    const bool timeout = !spi.finished();
    const bool ok = spi.success() && !timeout;

    if (timeout) {
        std::cerr << "Auth test timeout (" << timeout_sec << "s)." << std::endl;
    }

    api->RegisterSpi(nullptr);
    api->Release();

    std::cout << (ok ? "AUTH_TEST_PASS" : "AUTH_TEST_FAIL") << std::endl;
    return ok ? 0 : 2;
}
