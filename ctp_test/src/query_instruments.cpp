#include <iostream>
#include <thread>
#include <chrono>
#include <signal.h>
#include <cstring>
#include <vector>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <string>
#include "ThostFtdcTraderApi.h"

// 全局变量
CThostFtdcTraderApi* g_pTraderApi = nullptr;
bool g_bConnected = false;
bool g_bAuthenticated = false;
bool g_bLoggedIn = false;
bool g_bQueryComplete = false;
int g_nRequestID = 0;
std::vector<CThostFtdcInstrumentField> g_instrumentList;
std::ofstream g_outputFile;

struct TraderConfig {
    std::string front_address;
    std::string broker_id;
    std::string user_id;
    std::string password;
    std::string app_id;
    std::string auth_code;
    std::string user_product_info;
};

static TraderConfig g_cfg;

static std::string trim(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end == std::string::npos ? std::string::npos : end - start + 1);
}

static bool loadTraderConfig(const std::string& path, TraderConfig& cfg)
{
    std::ifstream in(path);
    if (!in) return false;

    std::string line;
    std::string section;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = trim(line.substr(1, line.size() - 2));
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        if (section != "TRADER") continue;

        if (key == "FrontAddress") cfg.front_address = val;
        else if (key == "BrokerID") cfg.broker_id = val;
        else if (key == "UserID") cfg.user_id = val;
        else if (key == "Password") cfg.password = val;
        else if (key == "AppID") cfg.app_id = val;
        else if (key == "AuthCode") cfg.auth_code = val;
        else if (key == "UserProductInfo") cfg.user_product_info = val;
    }

    return !cfg.front_address.empty() &&
           !cfg.broker_id.empty() &&
           !cfg.user_id.empty() &&
           !cfg.password.empty();
}

// 交易回调类
class CTraderSpi : public CThostFtdcTraderSpi
{
private:
    CThostFtdcTraderApi* m_pTraderApi;

public:
    CTraderSpi(CThostFtdcTraderApi* pTraderApi) : m_pTraderApi(pTraderApi) {}

    // 连接成功回调
    virtual void OnFrontConnected() override
    {
        std::cout << "=== 连接成功 ===" << std::endl;
        g_bConnected = true;
        g_bAuthenticated = false;
        g_bLoggedIn = false;
        
        // 如果AppID或认证码为空，跳过认证直接登录
        if (g_cfg.app_id.empty() || g_cfg.auth_code.empty()) {
            std::cout << "=== AppID或认证码为空，跳过认证直接登录 ===" << std::endl;
            g_bAuthenticated = true;  // 标记为已认证，因为不需要认证
            reqUserLogin();
        } else {
            // 先发送认证请求
            reqAuthenticate();
        }
    }

    // 发送认证请求
    void reqAuthenticate()
    {
        std::cout << "=== 发送认证请求 ===" << std::endl;
        
        CThostFtdcReqAuthenticateField req;
        memset(&req, 0, sizeof(req));
        strncpy(req.BrokerID, g_cfg.broker_id.c_str(), sizeof(req.BrokerID) - 1);
        strncpy(req.UserID, g_cfg.user_id.c_str(), sizeof(req.UserID) - 1);
        strncpy(req.AppID, g_cfg.app_id.c_str(), sizeof(req.AppID) - 1);
        strncpy(req.AuthCode, g_cfg.auth_code.c_str(), sizeof(req.AuthCode) - 1);
        if (!g_cfg.user_product_info.empty()) {
            strncpy(req.UserProductInfo, g_cfg.user_product_info.c_str(), sizeof(req.UserProductInfo) - 1);
        }
        
        int ret = m_pTraderApi->ReqAuthenticate(&req, ++g_nRequestID);
        if (ret != 0) {
            std::cout << "认证请求失败，错误代码: " << ret << std::endl;
        } else {
            std::cout << "=== 认证请求已发送 ===" << std::endl;
        }
    }

    // 认证响应
    virtual void OnRspAuthenticate(CThostFtdcRspAuthenticateField *pRspAuthenticateField,
                                   CThostFtdcRspInfoField *pRspInfo,
                                   int nRequestID, bool bIsLast) override
    {
        if (pRspInfo && pRspInfo->ErrorID != 0) {
            std::cout << "=== 认证失败 ===" << std::endl;
            std::cout << "错误代码: " << pRspInfo->ErrorID << std::endl;
            std::cout << "错误信息: " << pRspInfo->ErrorMsg << std::endl;
        } else {
            std::cout << "=== 认证成功 ===" << std::endl;
            g_bAuthenticated = true;
            
            // 认证成功后发送登录请求
            reqUserLogin();
        }
    }

    // 发送登录请求
    void reqUserLogin()
    {
        std::cout << "=== 发送登录请求 ===" << std::endl;
        
        CThostFtdcReqUserLoginField req;
        memset(&req, 0, sizeof(req));
        strncpy(req.BrokerID, g_cfg.broker_id.c_str(), sizeof(req.BrokerID) - 1);
        strncpy(req.UserID, g_cfg.user_id.c_str(), sizeof(req.UserID) - 1);
        strncpy(req.Password, g_cfg.password.c_str(), sizeof(req.Password) - 1);
        
        int ret = m_pTraderApi->ReqUserLogin(&req, ++g_nRequestID);
        if (ret != 0) {
            std::cout << "登录请求失败，错误代码: " << ret << std::endl;
        } else {
            std::cout << "=== 登录请求已发送 ===" << std::endl;
        }
    }

    // 连接断开回调
    virtual void OnFrontDisconnected(int nReason) override
    {
        std::cout << "\n=== 连接断开 ===" << std::endl;
        std::cout << "断开原因代码: " << nReason << std::endl;
        
        // 常见错误码说明
        switch (nReason) {
            case 0x1001:
                std::cout << "错误: 网络读失败" << std::endl;
                break;
            case 0x1002:
                std::cout << "错误: 网络写失败" << std::endl;
                break;
            case 0x2001:
                std::cout << "错误: 接收心跳超时" << std::endl;
                break;
            case 0x2002:
                std::cout << "错误: 发送心跳失败" << std::endl;
                break;
            case 0x2003:
                std::cout << "错误: 收到错误报文" << std::endl;
                break;
            default:
                std::cout << "未知错误码" << std::endl;
                break;
        }
        
        std::cout << "请检查：" << std::endl;
        std::cout << "  1. 服务器地址是否正确: " << g_cfg.front_address << std::endl;
        std::cout << "  2. 网络连接是否正常" << std::endl;
        std::cout << "  3. 防火墙是否允许连接" << std::endl;
        
        g_bConnected = false;
        g_bAuthenticated = false;
        g_bLoggedIn = false;
    }

    // 登录响应
    virtual void OnRspUserLogin(CThostFtdcRspUserLoginField *pRspUserLogin, 
                               CThostFtdcRspInfoField *pRspInfo, 
                               int nRequestID, bool bIsLast) override
    {
        if (pRspInfo && pRspInfo->ErrorID != 0) {
            std::cout << "=== 登录失败 ===" << std::endl;
            std::cout << "错误代码: " << pRspInfo->ErrorID << std::endl;
            std::cout << "错误信息: " << pRspInfo->ErrorMsg << std::endl;
        } else {
            std::cout << "=== 登录成功 ===" << std::endl;
            std::cout << "交易日: " << pRspUserLogin->TradingDay << std::endl;
            g_bLoggedIn = true;
            
            // 查询全部合约信息
            queryAllInstruments();
        }
    }

    // 查询全部合约信息
    void queryAllInstruments()
    {
        std::cout << "=== 开始查询所有合约 ===" << std::endl;
        
        CThostFtdcQryInstrumentField req;
        memset(&req, 0, sizeof(req));
        
        int ret = m_pTraderApi->ReqQryInstrument(&req, ++g_nRequestID);
        if (ret == 0) {
            std::cout << "=== 查询请求已发送 ===" << std::endl;
        } else {
            std::cout << "=== 查询请求失败，错误代码: " << ret << std::endl;
        }
    }

    // 查询合约信息响应
    virtual void OnRspQryInstrument(CThostFtdcInstrumentField *pInstrument, 
                                   CThostFtdcRspInfoField *pRspInfo, 
                                   int nRequestID, bool bIsLast) override
    {
        if (pRspInfo && pRspInfo->ErrorID != 0) {
            std::cout << "=== 查询合约信息失败 ===" << std::endl;
            std::cout << "错误代码: " << pRspInfo->ErrorID << std::endl;
            std::cout << "错误信息: " << pRspInfo->ErrorMsg << std::endl;
        } else if (pInstrument) {
            // 保存合约信息
            g_instrumentList.push_back(*pInstrument);
            
            // 每100个合约输出一次进度
            if (g_instrumentList.size() % 100 == 0) {
                std::cout << "已获取 " << g_instrumentList.size() << " 个合约..." << std::endl;
            }
        }
        
        if (bIsLast) {
            std::cout << "=== 查询完成，共获取 " << g_instrumentList.size() << " 个合约 ===" << std::endl;
            
            
            // 输出到文件
            writeToFile();
            g_bQueryComplete = true;
        }
    }

    // 写入文件
    void writeToFile()
    {
        // 生成文件名（带时间戳）
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm* tm = std::localtime(&time_t);
        
        std::ostringstream filename;
        filename << "instruments_" 
                 << std::put_time(tm, "%Y%m%d_%H%M%S") 
                 << ".csv";
        
        g_outputFile.open(filename.str(), std::ios::out);
        if (!g_outputFile.is_open()) {
            std::cout << "=== 无法创建输出文件 ===" << std::endl;
            return;
        }
        
        std::cout << "=== 正在写入文件: " << filename.str() << " ===" << std::endl;
        
        // 写入CSV表头
        g_outputFile << "合约代码,合约名称,交易所,产品代码,产品类型,交割年份,交割月份,"
                     << "市价单最大下单量,市价单最小下单量,限价单最大下单量,限价单最小下单量,"
                     << "合约数量乘数,最小变动价位,创建日,上市日,到期日,"
                     << "开始交割日,结束交割日,合约生命周期状态,当前是否交易,"
                     << "持仓类型,持仓日期类型,多头保证金率,空头保证金率,"
                     << "是否使用大额单边保证金算法,基础商品代码,执行价,期权类型,"
                     << "合约基础商品乘数,组合类型,交易所合约代码" << std::endl;
        
        // 写入数据
        size_t writtenCount = 0;
        for (const auto& inst : g_instrumentList) {
            g_outputFile << inst.InstrumentID << ","
                         << "\"" << inst.InstrumentName << "\","
                         << inst.ExchangeID << ","
                         << inst.ProductID << ","
                         << inst.ProductClass << ","
                         << inst.DeliveryYear << ","
                         << (int)inst.DeliveryMonth << ","
                         << inst.MaxMarketOrderVolume << ","
                         << inst.MinMarketOrderVolume << ","
                         << inst.MaxLimitOrderVolume << ","
                         << inst.MinLimitOrderVolume << ","
                         << inst.VolumeMultiple << ","
                         << inst.PriceTick << ","
                         << inst.CreateDate << ","
                         << inst.OpenDate << ","
                         << inst.ExpireDate << ","
                         << inst.StartDelivDate << ","
                         << inst.EndDelivDate << ","
                         << inst.InstLifePhase << ","
                         << inst.IsTrading << ","
                         << inst.PositionType << ","
                         << inst.PositionDateType << ","
                         << inst.LongMarginRatio << ","
                         << inst.ShortMarginRatio << ","
                         << inst.MaxMarginSideAlgorithm << ","
                         << inst.UnderlyingInstrID << ","
                         << inst.StrikePrice << ","
                         << inst.OptionsType << ","
                         << inst.UnderlyingMultiple << ","
                         << inst.CombinationType << ","
                         << inst.ExchangeInstID << std::endl;
            
            writtenCount++;
            
            // 每1000行刷新一次缓冲区
            if (writtenCount % 1000 == 0) {
                g_outputFile.flush();
            }
            
            // 检查写入错误
            if (g_outputFile.fail()) {
                std::cout << "=== 写入文件时发生错误，已写入 " << writtenCount << " 个合约 ===" << std::endl;
                break;
            }
        }
        
        // 确保所有数据都写入
        g_outputFile.flush();
        g_outputFile.close();
        
        std::cout << "=== 文件写入完成，共写入 " << writtenCount << " 个合约 ===" << std::endl;
        if (writtenCount != g_instrumentList.size()) {
            std::cout << "=== 警告：写入数量(" << writtenCount << ")与查询数量(" 
                      << g_instrumentList.size() << ")不一致 ===" << std::endl;
        }
    }
};

// 信号处理函数
void signalHandler(int signum)
{
    std::cout << "\n=== 收到退出信号，正在关闭... ===" << std::endl;
    if (g_outputFile.is_open()) {
        g_outputFile.close();
    }
    if (g_pTraderApi) {
        g_pTraderApi->Release();
        g_pTraderApi = nullptr;
    }
    exit(signum);
}

int main(int argc, char* argv[])
{
    std::string config_path = "config/config.ini";
    if (argc > 1) {
        config_path = argv[1];
    }
    if (!loadTraderConfig(config_path, g_cfg)) {
        std::cout << "加载配置失败: " << config_path
                  << "，需要 [TRADER] FrontAddress/BrokerID/UserID/Password" << std::endl;
        return -1;
    }

    // 注册信号处理函数
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    std::cout << "=== CTP 合约查询程序启动 ===" << std::endl;
    std::cout << "配置文件: " << config_path << std::endl;

    // 创建交易API实例
    g_pTraderApi = CThostFtdcTraderApi::CreateFtdcTraderApi("./data");
    if (!g_pTraderApi) {
        std::cout << "创建交易API实例失败" << std::endl;
        return -1;
    }

    // 创建回调实例
    CTraderSpi traderSpi(g_pTraderApi);
    
    g_pTraderApi->RegisterSpi(&traderSpi);

    // 注册前置地址
    std::cout << "注册前置地址: " << g_cfg.front_address << std::endl;
    g_pTraderApi->RegisterFront(const_cast<char*>(g_cfg.front_address.c_str()));

    // 初始化API
    std::cout << "初始化API..." << std::endl;
    g_pTraderApi->Init();

    std::cout << "正在连接服务器..." << std::endl;

    // 记录开始时间，用于超时检测
    auto startTime = std::chrono::steady_clock::now();
    const int CONNECT_TIMEOUT_SECONDS = 30;
    int lastPrintTime = -1;

    // 主循环
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        auto currentTime = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(currentTime - startTime).count();
        
        if (!g_bConnected) {
            // 每5秒打印一次等待信息
            if (elapsed != lastPrintTime && elapsed > 0 && elapsed % 5 == 0) {
                std::cout << "等待连接中... (" << elapsed << "秒)" << std::endl;
                lastPrintTime = elapsed;
            }
            
            // 超时退出
            if (elapsed > CONNECT_TIMEOUT_SECONDS) {
                std::cout << "=== 连接超时（" << CONNECT_TIMEOUT_SECONDS << "秒），可能原因：" << std::endl;
                std::cout << "  1. 网络连接问题" << std::endl;
                std::cout << "  2. 服务器地址错误: " << g_cfg.front_address << std::endl;
                std::cout << "  3. 服务器不可达或端口被防火墙阻挡" << std::endl;
                std::cout << "  4. 服务器维护中" << std::endl;
                break;
            }
            continue;
        }
        
        if (!g_bLoggedIn) {
            continue;
        }
        
        if (g_bQueryComplete) {
            std::cout << "=== 程序执行完成，退出 ===" << std::endl;
            break;
        }
    }

    // 清理资源
    if (g_outputFile.is_open()) {
        g_outputFile.close();
    }
    if (g_pTraderApi) {
        g_pTraderApi->Release();
        g_pTraderApi = nullptr;
    }

    return 0;
}
