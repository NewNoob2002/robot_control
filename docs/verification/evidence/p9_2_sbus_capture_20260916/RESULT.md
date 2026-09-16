# P9.2 首次目标适配器采集

状态：本次有界静态采集通过；不是 P9.2 完整验收。
操作员明确授权“适配器采集”。2026-09-16 10:44:42–10:44:52（Asia/Shanghai），
对应 UTC 02:44:42–02:44:52，在 robot-dev / lubancat 的 /dev/ttyACM0 执行。

适配器：QinHeng USB Single Serial，VID:PID 1a86:55d3，序列号 586D017868，
cdc_acm；稳定链接见 adapter-before.txt。不由 VID/PID 猜测具体芯片或线路。
观察工具 SHA256：6d6cb2fb9bb040cfbac28dc7ad516e939a48ef583ef9e6b81ab973298adc3b45，
与本地 P9.2 交叉产物完全一致，传输后复核通过。源码提交 1c235cc。

远端命令（在独立用户 staging 中）：

```bash
timeout --signal=TERM --kill-after=2s 15s ./robot-control-sbus-observer \
  --device /dev/ttyACM0 --duration-ms 10000 > capture.log 2> capture.stderr
```

- 配置回读 100000、8 data bits、even parity、2 stop bits、PARMRK。
- 1428 次 read、1428 帧、35700 个去除 PARMRK 编码后的原始 SBUS 字节。
- 每帧 25 bytes，头 0x0f、尾 0x00；独立 Python 位流解码与工具输出的全部
  16 通道和 raw flags 逐帧相符。全部 flags=0，lost/failsafe 均 0。
- 无 rejected/error/discontinuity，单会话 1；接收间隔最小 6.195873 ms、
  中位数 6.999415 ms、最大 7.805874 ms。这是用户态接收间隔，不是链路硬实时保证。
- CH1=1006..1007，CH3=994..995，CH6=200，CH7=1000。
  操作员未提交对应动作／档位位置确认，不能将这些值当作已完成校准。
- 正常 deadline 结束、退出码 0；stderr 为空；采集前后 lsof 没有报告占用者。
  ModemManager 处于 active，但 mmcli 报告无 modem；未停服务、未修改服务配置。
- 未发送 UART 数据、未执行显式 DTR/RTS/reset 命令、未访问 CAN 或驱动。
  tty 正常打开／配置／关闭属于本次采集；不宣称内核打开过程完全没有控制线行为。
- 远端用户 staging 保留在 /home/cat/.cache/robot-control/staging/p92-1c235cc-6d6cb2fb；
  不覆盖原 /opt/robot-control 目录。观察进程已结束，tty 描述符已关闭。

完整文本证据为 capture.log.gz，解压后的 SHA256：
2de69978783a5a7a081cc25cb6ca24f19bc6e683e0262b623a420c89f0d4809f。
analysis.json 保存通道范围与统计；SHA256SUMS 覆盖本目录原始和派生记录。

仍未验证：实际操纵与 CH1/CH3/CH6/CH7 的对应、发射机断电后的 lost/failsafe、
物理断开与恢复、目标 SIGTERM、适配器线路／电压／反相波形。
驱动配置回读加上成功解码不代替物理电气测量；SBUS 无 CRC，未检测到异常
不证明不存在未报告丢帧或负载位错误。本记录的授权不作为未来硬件操作授权。
