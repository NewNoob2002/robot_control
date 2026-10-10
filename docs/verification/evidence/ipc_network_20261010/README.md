# DDS UDP 访问阻断排查 — 2026-10-10

**已确认主机 UFW 丢弃新的 DDS UDP 入站流量。** 板端可达、报文已到主机网卡，
但到达网卡不等于通过防火墙进入 ROS 接收程序。应用或 ROS 节点无需为此修改。
当前 TCP 诊断通信仍正常，UDP 尚未放行。

| 检查 | 证据与结果 |
| --- | --- |
| 当前路由 | host `192.168.110.198` 与 board `192.168.110.161` 同网段直达；之前 host 为 `192.168.1.21`，board 看到 NAT 后地址 `192.168.110.155` |
| 默认 DDS | 0 条状态；抓到 111 个 RTPS 包，其中 84 个板端单播发往 host `53910`，两端 multicast 报文也已出现 |
| 更新地址后的显式 UDP | 0 条状态；抓到 111 个 RTPS 包，其中 96 个板端单播发往 host `54710`；SPDP 广告地址与当前 IP 一致 |
| 主机 UFW | 服务 active，默认 input DROP；内核 `[UFW BLOCK]` 明确记录 board→host 的 UDP `53910` 和 `54710` 被丢弃 |
| 独立 UDP socket 对照 | 板端主动发送的四包均被网卡抓到，host 两个已绑定监听 socket 收到 0 包；host 发起请求后的同 tuple PONG 收到 1 包 |
| 同条件 TCP | 两个 fixture 实例、229 条有效状态、两次过期事件；退出及新实例恢复通过 |
| C++ LSP | 六文件逐项主动复查通过，随后原六文件批量主动复查也 6/6 clean、0 timeout；旧超时未复现 |
| UFW 最小方案 | 本机 UFW 0.36.2 解析器验证 allow 和 rollback 语法，未构造 backend、未应用规则 |

独立 UDP 测试排除了 DDS 编码、QoS 或发现协议作为这次 socket 收不到包的原因。
同 tuple 的请求/回复可被现有连接跟踪策略接受；Fast DDS UDP 使用独立发送端口，
板端回发到广告的监听端口时形成新的入站流量，被当前 UFW 策略阻断。
TCP 使用由 host 发起的连接，因此在现有策略下正常。无需关闭整个防火墙。

旧跨网段网络包含 NAT，且旧显式配置广告旧 host IP，不能直接用于当前网络。
本次已更新地址再测；即便没有该 NAT 路径，主机入站过滤仍是明确阻断点。
旧时刻没有抓包，不能把所有历史失败唯一归因于当前 UFW，也未证明旧网关的全部策略。

## 最小处理方案（未应用）

固定 UDP 配置使用 host discovery `54710` / data `54711`，board `54720` / `54721`。
仅允许该板端 IP 进入主机当前 Wi-Fi 接口的两个 host 端口：

```sh
sudo /usr/bin/ufw allow in on wlan0 proto udp \
  from 192.168.110.161 to 192.168.110.198 port 54710:54711 \
  comment 'robot-control diagnostic UDP'
```

`UDP_RULE_PROPOSAL.json` 保存精确 argv、范围和对应删除语法。规则仅覆盖这一对显式
UDP test profiles；不自动放行默认 multicast、额外 ROS 参与者或其他来源。
应用需要主机 root 权限和对全局防火墙变更的明确授权。本轮为排查，未添加规则、
关闭防火墙、切换 Wi-Fi、修改路由或更改板端网络配置。

后续获准时先重新核对 IP/接口、现有规则及规则顺序，只记录实际新加的规则；重复/既有
规则不得当作本轮新增后删除。复测需验证独立 socket 的四个主动包可接收，以及相同
显式 UDP 配置的 DDS 状态、500ms 接收过期和两个 fixture 实例恢复。没有这项复测，
不能标记 UDP 已修复。维持现有 TCP 配置可继续本阶段诊断，不需要这次防火墙修改。

## 原始记录与边界

- `default.pcap.gz`、`udp.pcap.gz`、`port-probe.pcap.gz`：仅保存指定 peer/test ports 的
  IPv4 UDP RTPS 或显式测试标签；capture 无 promiscuous mode，40/40/10 秒有界。
- `*.capture.json`：网卡流向计数与有界 SPDP locator 解码。完整 PCAP 保留，未支持的
  SPDP 编码不伪造解码结果。
- `ufw-block-final.log` 与 `ufw-service.log`：直接内核丢弃记录及服务状态；完整
  `ufw status` 因无 root 权限不可读，未通过修改安全设置绕过限制。
- `tcp-dds.json`、压缩状态轨迹、execution JSON：真实原生 Humble与锁定host镜像的
  device-free 状态验证。反馈为 fixture 合成值，不证明物理 CAN/UART/驱动行为。
- `LSP.json`：本轮主动逐文件与批量工具结果。旧超时历史保留；当前不能复现，
  没有证明旧内部调度/缓存/负载的具体原因。
- 两个辅助探测先用了 Pi 的隔离 Python（无法 import 系统 UFW）、误读了 parser
  response 结构；改用 `/usr/bin/python3` 和已读的 `response.data['rule']` 后校验通过。
  均未进入 firewall backend，不是 UFW 服务故障或部分写入。

所有 fixture/node/capture 容器和测试 socket 已收尾。无物理 CAN/UART 访问、驱动/EEPROM
操作、生产发布或源代码行为变更；`.clangd` 及既有用户改动保持。
本地输出目录沿用 `out/verification/ipc-network-diagnosis-20261009` 标签，实际测试日期为
2026-10-10，以 PCAP/内核时间和 `RESULT.json` 为准。旧
[2026-10-09 验证证据](../ipc_link_20261009/README.md)保持原样。

`SHA256SUMS` 固定本档案文件；历史授权只证明这次诊断范围，不是应用 UFW 或物理运行许可。
