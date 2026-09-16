# P9.2 — Linux UART 与 SBUS 只读观察验证

日期：2026-09-16。状态：**软件实现与离线验证通过；P9.2 部分完成，首次实机静态采集通过，其余物理验收待完成**。
基点 `bf5b34053732209be5dd62ad3c510f6b4429996f`，分支 `codex/phase9-sbus-development`。
本节点不实现 P9.3 健康／命令快照，也不关闭 P6 或授予运动权限。

## 输入与交付

操作员确认 R8FM、已完成反相、沿用 CH1/CH3/CH6/CH7 角色；
2026-09-16 补充串口路径 `/dev/ttyACM0`。随后操作员授权适配器采集，已在目标完成 10 秒有界只读观察。
已识别适配器与 by-id 稳定链接，100000/8E2 配置回读与原始帧解码成功；
具体接线／电压和各通道实际动作对应仍待确认，见下方首次采集记录。

- `platform/linux/uart/SerialPort` 增加 100000、termios2 TCGETS2/TCSETS2、
  严格的 8-bit raw 配置回读、队列查询和输入清空。不支持或回读不符明确报错。
  保留原有标准速率、非阻塞读取和取消接口。hangup 即使伴随 readable 也报错。
- `input/sbus/linux/Reader` 复用 P9.1 parser：单 owner、无后台线程、每次最多
  256 个内核字节、等待上限 20 ms；每批最多 256 个有序 parser 事件。
  返回 steady_clock 接收时间、非零会话、完整原始 flags 与全部通道。
  这些是输入观察，不是有效命令或重新授权。
- `robot-control-sbus-observer`：默认 100000 8E2、1 秒；显式 device，最长 60 秒，
  最多 4096 条非空／不连续 read 记录。输出配置、内核原始字节、接收时间、
  会话、帧／拒绝事件与终止摘要。`--parity none` 仅用于明确选择的诊断／PTY；
  没有自动降级。使用 signalfd 响应 SIGINT/SIGTERM，无自动重连。
- ELF 审计改为验证 `ioctl` 和真正的 `SerialPort::open/configuration`，不再要求
  已被 termios2 替代的 `tcsetattr`。保留其他 ABI／依赖／版本与平台符号门槛，
  Phase 1 脚本回归增加缺少 ioctl／UART 方法时必须拒绝的用例。

## 数据连续性与限制

- 配置启用 `INPCK|PARMRK`；内核正常 `ff` 表示为 `ff ff`，错误标记以
  `ff 00` 开始。Reader 跨 read 解转义；发现错误标记立即关闭端口并返回
  `EILSEQ`，该批任何帧均不发布。必须显式 reopen；不会把尚未到来的错误尾字节
  当成新帧。`kernel_raw` 是 PARMRK 编码后的内核字节，不是直接的电气波形。
- 启动／重开会 flush 内核队列、清除残帧和转义状态，并递增会话。
  会话达到 uint64 上限后失败关闭，不循环复用；该极限路径为代码审查，未穷举运行。
- 默认服务间隔上限 50 ms（可注入 1..1000 ms）。达到边界、单次读满 256 bytes、
  读后队列仍非空，或残帧／转义片段持续无新字节达到上限时，丢弃该批和队列，
  清除解析状态，报告 discontinuity 并建立新会话；不将旧批作为新命令发布。
  discontinuity 数值：0 无、1 服务间隔、2 积压、3 残帧超时。
  队列检查故意保守，可能舍弃恰好新到达的数据。
- 任何 read／poll／ioctl 错误或取消均关闭 reader，调用方必须先处理失效再 reopen。
  空批表示本次未形成输入，不能刷新 P9.3 的最近有效帧时间。P9.3 必须处理
  error、discontinuity、会话变化和 lost/failsafe 后才能消费后续观察。
- 接收时间是用户态读取时间；驱动／USB 内部的历史缓存、小于检测门槛的队列、
  未上报的丢字节／overrun 没有逐字节时间或完整计数保证。普通 UART/SBUS 无 CRC，
  不能把本节点理解为“所有错误均可检测”或“数据一定来自刚刚采样的发射机”。
- 观察工具 stdout 使用非阻塞写并恢复原 flags；输出满、短写或断管会失败退出，
  不静默丢记录。输出应由持续读取的管道或正常文件接收。普通 Linux 调度及
  设备／文件系统内核阻塞不构成硬实时保证；PTY 不能证明电平、反相、波特率或奇偶校验。
- 链接检查确认 observer 包含 reader/parser/UART，没有 CANopen、CanSocket 或
  socket/send/sendto/sendmsg 符号；不连接 CAN 发送路径。本次只访问授权 UART，未访问实体 CAN。

## 实际验证

| 检查 | 实际结果 |
| --- | --- |
| 窄测试 `sbus_parser_contract`、`sbus_reader_pty`、`sbus_observer_cli` | PASS；包含逐字节、多帧 flags 顺序、PARMRK 跨读、同批错误、超时、残帧超时、积压、重开、取消优先、设备断开、参数错误、SIGTERM 和输出背压 |
| 新目录 host Debug，GCC 16.2.1 | 31/31 PASS，无 skip |
| 新目录 host Release，GCC 16.2.1 | 31/31 PASS，无 skip |
| Clang 22.1.8 ASan/UBSan | 31/31 PASS，无 skip；本机 detect_leaks=0，未宣称 LeakSanitizer 通过 |
| clang-format、范围内 clang-tidy | PASS；使用项目已有 analyzer/bugprone/performance/portability 检查族 |
| Phase 1 脚本回归、修改脚本 ShellCheck、Python 语法、git diff --check | PASS |
| 锁定 Docker 全工程 aarch64 Debug | 53 个构建步骤 PASS；原始已验证目标 sysroot；不是容器文件系统替代 |
| platform-probe ELF 审计 | PASS：解释器、依赖、符号版本、更新后的 UART 链接、无 RPATH |
| SBUS observer 独立 ELF／链接检查 | PASS：aarch64、解释器、依赖、GLIBC/GLIBCXX/CXXABI、无 RPATH、无 CAN 发送符号 |
| RK3588／R8FM 首次 10 秒静态采集 | PASS：1428 帧，默认 100000/8E2 回读，独立原始解码匹配，正常退出 |
| 通道动作对应、失联／恢复、目标 SIGTERM、物理电气验证 | **未执行，P9.2 尚未完整验收** |

完整 host CTest 在沙箱外运行已有的隔离 managed-vcan 回归，无实体 CAN 接口操作。
PTY 配置使用显式 100000 8N2；另测试默认 8E2 被 PTY 清除 PARENB 后必须拒绝。
奇偶／framing/break 错误用 read syscall 包装器注入，不冒充 PTY 产生真实电气错误。

构建／测试入口（三个目录均从新目录配置）：

```bash
cmake -S . -B out/build/p92-host-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build out/build/p92-host-debug --parallel 2
ctest --test-dir out/build/p92-host-debug --output-on-failure

cmake -S . -B out/build/p92-host-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build out/build/p92-host-release --parallel 2
ctest --test-dir out/build/p92-host-release --output-on-failure

cmake -S . -B out/build/p92-sanitizer -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build out/build/p92-sanitizer --parallel 2
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir out/build/p92-sanitizer --output-on-failure

ROBOT_CONTROL_SYSROOT="$PWD/sysroots/rk3588-ubuntu2204" \
ROBOT_CONTROL_SYSROOT_LOCK="$PWD/sysroots/rk3588-ubuntu2204.lock.json" \
ROBOT_CONTROL_PRESET=rk3588-debug ./scripts/build/build_rk3588.sh
```

静态检查命令：

```bash
clang-tidy -p out/build/p92-sanitizer \
  platform/linux/uart/serial_port.cpp input/sbus/linux/reader.cpp \
  tools/sbus_observer/main.cpp tests/integration/sbus_reader_tests.cpp \
  '-header-filter=(input/sbus/|platform/linux/uart/)' \
  '-checks=-*,clang-analyzer-*,bugprone-*,performance-*,portability-*,-portability-avoid-pragma-once' \
  '-warnings-as-errors=*'
./scripts/test/test_phase1_scripts.sh
```

clang-tidy header 范围限于本次输入与 UART，未改动已有 `Result::value()` 的调用前置约定；
所有新调用先检查 `ok()`。测试中的三处 NOLINT 分别说明固定底层 enum 的非法值注入、
GNU ld `--wrap` 要求的两个保留名称；不关闭其他代码的同类检查。

## 证据位置、源码与失败历史

- CTest 日志：`out/build/p92-{host-debug,host-release,sanitizer}/Testing/Temporary/LastTest.log`。
- 静态／脚本日志：`out/p92-clang-tidy.log`、`out/p92-phase1-scripts.log`。
- cross：`out/p92-cross.log`；元数据 `out/artifacts/rk3588-debug/build-metadata.json`。
  该元数据 artifact 字段仍指向 platform-probe；observer 身份单列如下。
- observer 检查：`out/p92-audit-observer.py` 与 `out/p92-observer-elf.log`，仅对本地 ELF 做
  readelf/file 检查并与目标 sysroot 库提供的版本集合比较，不执行目标程序。
- cross 镜像 `rk3588-cross:phase1-20260814`，GCC 11.4、CMake 3.22.1；镜像与
  sysroot 身份沿用 [P9.1 记录](P9_1_SBUS_PARSER_BASELINE.md)，没有修改锁或 sysroot。
- 交叉构建记录的是基点上的 dirty 源码快照，不能称为已提交的 clean Release 构建。
  快照 SHA256：`6e761bfb810a89d2009f4b25ad0fcd2241b50a81b93c65d96d45ac1d638314c7`。
- 初次复用旧 host 目录在沙箱内的 platform integration 出现 logging 断言失败；
  未修改日志模块。新目录、沙箱外完整回归通过；旧失败不计 PASS。
- 首次 cross 编译成功但 ELF 门槛因旧 `tcsetattr` 要求失败，退出码 8；
  `out/p92-cross-initial.log` 保留。更新门槛并补负向回归后重建最终源码通过。
- 初次静态检查的无效 move／enum 大小已修复；原始日志
  `out/p92-clang-tidy-initial.log` 保留。局部测试注入说明与 header 范围见上文。

| 文件／产物 | SHA256 |
| --- | --- |
| `platform/linux/uart/serial_port.cpp` | `dcd350a1527b93aa5b5cda2eca9f7184e02ec18c3e1fb11fed8ea095da8d6936` |
| `input/sbus/linux/reader.hpp` | `c5ebd8ebd504431b81c7cc5b1b5dc6e638a4263e9f04fcc4eb70af929ffb08a1` |
| `input/sbus/linux/reader.cpp` | `e10581d0c97452dbe26d6af738be6bcf83e87a0125e0cc6b9561d06d917d9a14` |
| `tools/sbus_observer/main.cpp` | `a40934f723675bc55bba1d355afb2d119d21a7b39d700950e7a0ce1dddd1347c` |
| `tests/integration/sbus_reader_tests.cpp` | `ec19dff6571befcc5d87e61e2266dafc532b076266d103bb636e22f8f4ad4b0c` |
| `scripts/test/test_sbus_observer.py` | `201b7645ca8e3cb70d649b9cebc709745d4e3a0752a21e1a77f378ac6a1069b7` |
| `out/build/cross/rk3588-debug/tools/sbus_observer/robot-control-sbus-observer` | `6d6cb2fb9bb040cfbac28dc7ad516e939a48ef583ef9e6b81ab973298adc3b45` |

## 首次已授权实机采集

操作员于 2026-09-16 明确授权适配器采集，10:44:42–10:44:52（Asia/Shanghai）
完成默认 100000/8E2 的 10 秒观察：1428 帧，raw flags 全 0，无 rejected／
discontinuity／error，退出码 0，接收间隔中位数约 7 ms。原始 35700 bytes
独立解码与工具逐帧相符。CH1=1006..1007、CH3=994..995、CH6=200、CH7=1000。
这仅证明该次静态输入可读，尚未确认实际操纵和通道的对应关系。

完整 [结果及原始证据](evidence/p9_2_sbus_capture_20260916/RESULT.md) 已保存；
本次没有 CAN 发送、驱动操作或生产部署。远端使用独立用户 staging，进程已退出。

## 剩余实机验收流程（静态采集已执行；其余待后续约定）

1. 固定目标主机与 `/dev/ttyACM0` 的适配器身份、权限、接线／电平、反相链路、透明输出
   能力；确认独占读取、开关该适配器的影响和退出后恢复方式。不猜测本机同名设备。
2. 单独授权上述目标的有界只读采集和开发 artifact staging 后，核验二进制哈希，
   运行默认 100000 8E2，保存开始配置回读、全部 raw／frame／error 和终止摘要。
   命令形态为 `robot-control-sbus-observer --device /dev/ttyACM0 --duration-ms 10000`。
   首次已授权采集使用上述 10000 ms 参数；后续实验另行约定，不能以 `--parity none` 绕过 8E2 门槛。
3. 依次操作已确认的 CH1/CH3/CH6/CH7，核对观察值与实际动作；记录发射机失联时
   flags、停止发送／断开和恢复现象。P9.2 仅观察，健康恢复／授权行为留给 P9.3/P10。
4. 在单独约定的短采集中发送 SIGTERM，记录退出码 143、时间、完整日志和最终进程退出；
   结合设备证据验证速率、校验、停止位和反相，不将软件回读或 PTY 当作电气测量。
5. 任何配置不支持、日志不完整、超时或通道不符均保持未验收；通过后追加实机记录与
   独立提交/push。本次仅提交软件部分，不用 `complete P9.2` 或 PASS 标签冒充整节点通过。
