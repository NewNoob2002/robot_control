# 限定 UFW 规则与 UDP 复测 — 2026-10-10

**PASS：用户明确授权的规则已新增，显式 UDP 板端→host 诊断状态验证通过。**
原先的 UFW 阻断见[排查记录](../ipc_network_20261010/README.md)；旧失败原样保留。

## 实际变更

仅添加一个 IPv4 入站规则：`wlan0`，source `192.168.110.161`，destination
`192.168.110.198`，UDP `54710:54711`，comment `robot-control diagnostic UDP`。

```sh
sudo /usr/bin/ufw allow in on wlan0 proto udp \
  from 192.168.110.161 to 192.168.110.198 port 54710:54711 \
  comment 'robot-control diagnostic UDP'
```

原先两条 KDE Connect IPv4/IPv6 应用规则保留。UFW 仍 active，默认 incoming/routed deny、
outgoing allow。新增时原规则不存在，实际返回 `Rule added`；最终 numbered 列表中该规则
为第2条，IPv6原规则顺移，回退不得依赖可能改变的规则号。只在需要撤销本轮新增规则时使用
`RULE.json` 中的精确 delete argv；本轮验证成功后保留用户授权规则。
使用本机 pkexec 的系统认证完成操作，密码未传入工具或档案。

## 对照验证

| 项目 | 结果 |
| --- | --- |
| 规则前 | 默认/显式 UDP 0条状态；四个主动包到网卡但socket收到0；既有tuple回复可达 |
| 规则后独立 UDP | 54710/54711各收到2个主动包，共4；同tuple PONG通过 |
| 相邻未放行端口 | 54712两个主动包均到网卡，但已绑定socket收到0；范围边界通过 |
| 显式 UDP DDS | 两个fixture实例193+195=388条有效状态，全部进展/字段/无运动权限断言通过 |
| 静默与恢复 | 两次host-local 500ms接收过期，第二fixture实例恢复fresh，最终停止后不再fresh |
| 默认 DDS负向检查 | 仍0条状态；observer退出1符合未授权端口的预期，非本次显式UDP复测失败 |
| 捕获完整性 | explicit/default/probe三份PCAP记录与流向计数一致，无2MiB截断 |
| 收尾 | board fixture/node退出、stage socket不存在，54730/54820无监听；host测试socket/capture容器均收尾 |

`udp-execution.json` 中 board/host/capture 都退出0。`default-execution.json` 中 host退出1
作为预期负向结果保留，完整traceback未隐藏。`ufw-block-after.log`未出现新日志条目；UFW
low日志有速率限制，不把无日志作为放行/阻断证据。负向判定依据实际监听结果、PCAP和现有规则。

## 使用与范围

必须为参与的ROS进程选择本次 `host-udp.xml` / `board-udp-current.xml`，
`ROS_DOMAIN_ID=186`、`ROS_LOCALHOST_ONLY=0`、`RMW_IMPLEMENTATION=rmw_fastrtps_cpp`。
使用锁定Humble容器的host observer与板端原生Humble；节点/fixture身份保持原SHA256。
这些XML仅验证单个host参与者的固定监听端口；多个进程不能不经端口规划共同使用该配置。
默认multicast DDS和额外参与者端口没有放行，当前GUI wrapper仍使用默认DDS设置。

本次为fixture合成状态与交付新鲜度验证，无physical CAN/UART/驱动/EEPROM访问，
未改板端系统网络、生产程序、ROS运动接口或源码行为。没有跨主机CLOCK_MONOTONIC相减，
没有证明source-state age或远程probe适用性。本次验证服务退出/新实例恢复，
不新增UDP物理网络中断/同实例恢复或硬件HIL的验收。
IP改变后必须重新审查XML和规则目标；当前授权不扩展到其他地址、接口或端口。

`RESULT.json` 为机器判定，`RULE.json` 为变更与回退信息，压缩PCAP/JSONL为完整原始证据；
`SHA256SUMS`固定档案，`FILES.json`固定现有源代码身份。
