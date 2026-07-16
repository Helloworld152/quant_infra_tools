# CTP Test

`ctp_test/` 存放 CTP 基础测试与查询示例，源码、头文件、配置和运行脚本都位于该目录下，但构建入口已经统一到仓库根目录。

## 目录说明

```text
ctp_test/
├── CMakeLists.txt
├── config/
├── include/
├── lib/
├── src/
├── run.sh
├── run_auth.sh
├── run_qry.sh
└── run_for_quote.sh
```

对应可执行文件：

- `md_client`：行情客户端
- `query_instruments`：合约查询
- `auth_test`：穿透认证测试
- `for_quote_demo`：询价示例
- `trade_latest_price`：通过 TraderApi 批量查询配置中的合约，并输出 CSV

## 统一构建

在仓库根目录执行：

```bash
cmake -S . -B build
cmake --build build
```

构建产物输出到仓库根目录 `bin/`，与 `build/` 同级：

```text
quant_infra_tools/
├── bin/
└── build/
```

## 运行方式

可以直接使用本目录脚本；脚本会自动调用根目录统一构建：

```bash
./run.sh
./run_auth.sh [config_path]
./run_qry.sh
./run_for_quote.sh <instrument_id> <exchange_id> [config_path]
./run_trade_latest_price.sh [config_path]
```

也可以在仓库根目录手动运行：

```bash
./bin/md_client
./bin/auth_test ctp_test/config/config.ini
./bin/query_instruments
./bin/for_quote_demo au2412 SHFE ctp_test/config/config.ini
./bin/trade_latest_price ctp_test/config/config.ini
```

## 运行前提

- 需安装 `cmake` 和 `g++`
- 运行时依赖 `ctp_test/lib/` 下的动态库
- 配置文件默认位于 `ctp_test/config/`
- 程序会在当前工作目录下创建 `flow_*` 流控目录
- `trade_latest_price` 会读取 `[INSTRUMENTS] Instruments=...` 中的多个合约，并输出 `trade_latest_price_results.csv`

## 合约元数据脚本

可复用脚本 [instrument_meta_tool.py](/home/ruanying/ctp_api/ctp_test/instrument_meta_tool.py) 用于：

- 从原始合约 CSV 提取 `品种,合约乘数,最小变动单位`
- 将提取结果与参考文件做差异对比

示例：

```bash
python3 instrument_meta_tool.py build-meta \
  --csv instruments_20260701_135126.csv \
  --output instruments_20260701_135126_product_meta.csv

python3 instrument_meta_tool.py compare \
  --reference auto \
  --meta instruments_20260701_135126_product_meta.csv \
  --report auto_compare_report.txt

python3 instrument_meta_tool.py build-and-compare \
  --csv instruments_20260701_135126.csv \
  --reference auto \
  --output instruments_20260701_135126_product_meta.csv \
  --report auto_compare_report.txt
```
