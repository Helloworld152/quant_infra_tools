# quant_infra_tools

仓库按“大类一个文件夹”组织：

- `ctp_test/`：CTP 基础测试与查询示例（原根目录代码已迁入）
- `rohon_test/`：Rohon 相关测试示例
- `nettools/`：组播接收与 `pcap` 抓包示例
- `hf_ctp_md/`：高频行情采集模块

统一构建入口位于仓库根目录：

```bash
cmake -S . -B build
cmake --build build
```

可执行文件会输出到仓库根目录 `bin/`，与 `build/` 同级。运行某一类示例时，仍可进入对应目录执行其 `run*.sh` 脚本。
