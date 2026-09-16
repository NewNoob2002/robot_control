# P9.2 — 30 秒手动通道、失联／恢复与 SIGTERM 验证

日期：2026-09-16。**第二轮完成本次请求的 30 秒验证；第一轮保持失败**。
操作员明确授权本次试验，并在两轮开始前分别回复“准备好”。目标均为
robot-dev / lubancat，适配器 /dev/ttyACM0，序列号 586D017868，100000 8E2。
保持原接线和供电，只接收 SBUS；无 CAN、驱动、运动命令或生产服务变更。

## 第一轮：失败，不能计为 30 秒通过

- 开始：2026-09-16 11:01:34.906（Asia/Shanghai）。约 28.7 秒触发原工具的
  `capture record limit`，进程退出 1，未执行计划中的目标 SIGTERM。
- 4096 个 read 记录包含 4098 帧；其中 3676 帧 flags=0、71 帧 flags=4、351 帧
  flags=12。启动 3.264 ms 和 7.912 ms 各有一次 rejected。
- CH7 一直 1000，CH7 提示阶段 CH5 有变化。操作员确认看到了提示、完成关机再开机，
  两侧开关标签为 CH6/CH7。该差异保留，不据此将档位映射改到 CH5。
- 原始数据、目标执行脚本、桌面通知回执、异常堆栈和确认记录均在 `attempt1/`。
  修复后使用新的 staging 与结果目录，未覆盖原始失败记录。

## 修复及离线验证

原因是按 read 次数限制采集量，使约 143 帧/s 的输入在 4096 次读取后提前结束，
且上限随驱动分块变化。现在按 **1 MiB 内核原始字节**限制，与 read 分块无关；
仍保留最长 60 秒、非阻塞输出和输出错误退出。100000 8E2 的 60 秒输入即使
PARMRK 将每字节加倍，也不超过该预算。异常高速诊断输入仍会被预算拒绝。

- 先新增 PTY 回归：以独立小块跨过 4096 次读取，精确读到 1 MiB 后再多送 1 byte。
  旧二进制在第 4097 次报 record limit，保留 `software/budget-regression-before.log`。
- 修复后二进制接受精确预算，下一字节明确报 byte limit；原 CLI、取消、输出背压等回归通过。
- 最终源码 Debug／Release／ASan+UBSan 各 31/31；LeakSanitizer 未启用。
- clang-format、范围内 clang-tidy、锁定 Docker 全工程 aarch64 Debug 53 步及 ELF 审计通过。
  observer 另核查解释器、GLIBC/GLIBCXX/CXXABI、无 RPATH、无 CAN 发送链接。
- `software/` 保存构建元数据、原始测试／静态日志；源码是 `6069505` 加本次修复的
  dirty 快照，不宣称 clean Release。只修改观察工具预算与对应 CLI 测试。
- 原产物 SHA256：`6d6cb2fb9bb040cfbac28dc7ad516e939a48ef583ef9e6b81ab973298adc3b45`。
- 修复后产物 SHA256：`bd76d3fd3d77d2ade41c4ba5e28a884fca5ab24a8b53947fd44db5e11cc03fe8`。
  目标 runner 启动前重新核验此哈希、适配器序列号和串口占用。

## 第二轮：30 秒运行与退出通过

开始 2026-09-16 **11:13:52.787**（Asia/Shanghai；UTC 03:13:52.787）。
目标单调时钟在启动后 **30000.184252 ms** 发送 SIGTERM，进程退出码 **143**；
发信号到 wrapper 确认回收为 **15.993384 ms**，满足预设 1 秒退出门槛。
该耗时含调度、记录与 wait 的开销，是本次观察到的退出上界，不是硬实时保证。
工具内置 60 秒仅作备用期限；本轮实际由 30 秒 SIGTERM 终止，末尾摘要确认 signal=15。

| 观察项 | 第二轮结果 |
| --- | --- |
| 有效结构帧 | 4290；4286 个 read 记录 |
| 去除 PARMRK 后的字节 | 107295；其中有效帧 107250 bytes，启动非帧字节 45 |
| 独立原始解码 | 所有有效帧的 16 通道／flags 与工具逐帧一致；独立扫描也复现全部 3 个 rejected 候选 |
| 正常 flags=0 | 3870 帧 |
| flags=4（lost） | 72 帧 |
| flags=12（lost+failsafe） | 348 帧；合计 lost bit 置位 420 帧、failsafe bit 置位 348 帧 |
| 会话／不连续 | 会话 1；无 reader discontinuity 或 UART read error |
| 接收间隔 | read 间隔中位数 6.999476 ms，最大 7.907151 ms；初始有合并多帧读取 |
| 启动重同步 | 3.381 ms 一次、5.498 ms 两次候选拒绝；其后无 rejected |
| 采集结束 | 30 秒 SIGTERM；exit=143，正常 signal 摘要；lsof 结束后未报告占用 |

启动数据从帧中间开始并含短暂碎片／合并读取，故不能将全部接收字节当成完整帧，
也不能把启动突发中的帧视为独立的新鲜命令。USB 内部缓存字节年龄未知；这些观察
不解决 C2 已记录的缓存／时间戳限制。P9.3 仍须处理启动、rejected、会话与健康恢复。

## 通道对应与现场确认

第二轮操作员明确确认：CH7 阶段只操作标记 CH7 的开关，并完成低／中／高切换；
结束时发射机开启、摇杆中立、CH6 松开、CH7 中档。见 `attempt2/operator-confirmation.txt`。

| 角色 | 第二轮正常标志期间的观察 | 结论 |
| --- | --- | --- |
| 转向 CH1 | 左右操作覆盖 200..1800，结束约 1002 | 支持原 CH1 角色 |
| 油门 CH3 | 上下操作覆盖 200..1800，结束约 993 | 支持原 CH3 角色；不是完整校准 |
| 启停 CH6 | 200／1800 切换，结束 200 | 支持原 CH6 角色 |
| 档位 CH7 | 正常 flags 帧中出现 200／1000／1800，结束 1000 | 第二轮复核支持原 CH7，未修改映射 |

提示由目标单调时钟产生，通过本机桌面通知显示；操作员确认看到了提示。
真人动作有反应时间、阶段有交叠（例如 CH7 高档落在 20 秒提示之后），
`analysis.json` 的窗口只表示计划提示区间，不能将其当作物理动作的精确起止时间。
摇杆旁微调按钮未要求操作。本轮保留原通道契约，不修改中心／死区／档位配置。

## 失联与恢复

相对第二轮启动时刻：

- 23.967696 s 首次观察 flags=4；
- 24.471648 s 首次观察 flags=12；
- 26.907973 s 恢复 flags=0，之后持续至 SIGTERM；
- 失联期间接收机仍持续发帧，CH3 原始值变为 0；不能把“仍有字节”视为有效遥控输入。

计划 20 秒提示关机、24 秒提示开机。上述差值包含人工动作、发射机启动与链路行为，
不能据此宣称 RF 检测／恢复延迟或设置 P9.3 超时参数。这里只验证原始帧标志链，
没有健康命令生产者或机器人重新授权，也不宣称恢复会／不会驱动机器人。

## 证据与重算

`attempt1/`、`attempt2/` 各自保存原始 `capture.log.gz`、目标时间线、通知回执、
实际 runner、preflight、结果、退出后占用观察与操作员确认。通知调用全部成功，
但通知 API 成功本身不证明观看时间；观看与操作确认另记。

```bash
python3 docs/verification/evidence/p9_2_sbus_manual_20260916/analyze_capture.py \
  docs/verification/evidence/p9_2_sbus_manual_20260916/attempt1
python3 docs/verification/evidence/p9_2_sbus_manual_20260916/analyze_capture.py \
  docs/verification/evidence/p9_2_sbus_manual_20260916/attempt2
cd docs/verification/evidence/p9_2_sbus_manual_20260916
sha256sum --check SHA256SUMS
```

本轮完成用户要求的通道动作、发射机失联／恢复和目标 SIGTERM 验证。
真实 USB 拔插、接线电压／反相波形独立测量和完整通道标定未执行；不能外推为这些项目通过。
目标旧、新用户 staging 均保留，两个 observer 均已结束；不改变生产服务或 P6 状态。
本轮记录不能作为后续硬件试验的自动授权。
