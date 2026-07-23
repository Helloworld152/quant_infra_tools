# Nettools

`nettools/` 存放网络抓包与组播接收相关 demo。

## 目录说明

```text
nettools/
├── CMakeLists.txt
├── include/
├── src/
├── run_multicast.sh
├── run_pcap_live.sh
└── run_pcap_file.sh
```

对应可执行文件：

- `multicast_recv_demo`：按命令行参数加入 IPv4 组播并验证是否收到首包
- `pcap_recv_demo`：支持实时抓包和离线读取 `pcap/pcapng` 文件，验证是否抓到首包

## 运行方式

首次拉取后先初始化子模块：

```bash
git submodule update --init --recursive
```

```bash
./run_multicast.sh <multicast_ip> <port> [bind_ip] [timeout_sec]
./run_pcap_live.sh <interface> [timeout_sec]
./run_pcap_file.sh <pcap_file>
```

也可以在仓库根目录手动运行：

```bash
./bin/multicast_recv_demo 239.10.10.10 12345 0.0.0.0 5
./bin/pcap_recv_demo live eth0 5
./bin/pcap_recv_demo file /tmp/sample.pcap
```

## 判定规则

- 成功收到任意 1 个组播包，程序返回 `0`
- 成功抓到任意 1 个 `pcap` 数据包，程序返回 `0`
- 超时或未捕获到包，程序返回非 `0`

## 说明

- `multicast_recv_demo` 通过 `third_party/hft-common` 中的 `UdpMulticastReceiver` 复用组播接收逻辑
- `multicast_recv_demo` 传 3 个参数时走 ASM (`IP_ADD_MEMBERSHIP`)，传 4 个参数时走 IGMPv3 SSM (`IP_ADD_SOURCE_MEMBERSHIP`)
- `pcap_recv_demo` 优先包含系统 `libpcap` 头文件；如果本机未安装开发头文件，则使用仓库内的最小兼容声明链接系统 `libpcap`
- `pcap_recv_demo live` 通常需要 root、`CAP_NET_RAW` 或等效抓包权限
- 本机自发自收组播是否成功，取决于宿主机的组播路由、回送和接口配置
