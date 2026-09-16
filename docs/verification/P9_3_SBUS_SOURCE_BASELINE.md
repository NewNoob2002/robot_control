# P9.3 — SBUS 健康、映射与命令快照

日期：2026-09-16。状态：**P9.3 在 SBUS 输入健康、映射和只读快照范围验收通过（CLOSED）；提交后远端 CI 另行核对**。
开发基点：866feaf1279cafa5ed6164b6d2e6eb60b83576f2；分支 codex/phase9-sbus-development。

## 实现和约束

- [Source](../../input/sbus/source/source.hpp) 是纯 C++20 生产者，显式注入启动配置和单调时间。
  默认轴标定无效；构造时验证配置，不静默修正非法参数。不读取系统时钟、不创建线程、不持有设备／驱动对象。
- 配置校验覆盖四个互异通道、独立端点／中心／反向、输入死区、按钮／档位阈值、档位和全局限幅、输出死区、恢复帧数、超时与冷却。
  整数归一化和直接混控遵守 P9.0 契约；乘法使用 int64 中间值，不添加滤波／斜坡。
- 启动无效且禁用。拒绝、lost/failsafe、精确超时、UART 错误、失连续性、时钟／会话重放均撤销启用，并将实际发布的两个 rpm 清零。
  连续健康输入只恢复通信健康；中立的新 release→press 才允许启用。非中立／冷却期间的按压被消耗，不排队。
- 每次合法启用增加输入授权代际并先发布零；同一 read 后续非零帧也不能改变该次零发布。
  正常截止与 SIGTERM 调用 stop，发布 shutdown 原因及零／无效值。输入授权不替代系统安全授权。
- 原始帧、归一化轴和候选 rpm 保留为诊断；有效命令与通信健康分开。
  最新故障及其 raw flags 不会被后续健康帧擦除；不是无限历史事件日志。
- 一个 mutex 保护整批处理和按值快照，支持并发读者，不暴露内部可变引用。
  重复 snapshot/tick（状态未变）不改变时间、内容或序号。sequence 的最终值保留给永久失效快照，授权计数不回绕，恢复计数饱和。
- [Linux bridge](../../input/sbus/linux/source_bridge.hpp) 转交 Reader 的全部有序事件、空批、失连续性与错误。
  设备错误详情仍保留在 Result.status()，调用方负责记录。UART错误／取消／stop后要求更大的会话，无自动重连。

生产者必须在无新输入时持续 tick，或通过 bridge 处理 Reader 空批；snapshot 只读取，不做健康更新。
P10 控制 owner 仍需检查时效／会话／系统授权，并验证调度与失效传播上限。本次不修改 arbiter 或 SafetyManager。
mutex 和普通 Linux 调度不构成硬实时保证。

### 合并帧与启动碎片

恢复计数每个严格更新的接收批次最多增加一次，按钮边沿只检查该批首个健康事件。
同一批发生故障后，后续健康帧仅更新诊断，不恢复健康或启用。
相同／倒退的批次时间与旧会话明确撤权；同一批内各帧共享接收时间则允许。
这是对 P9.2 合并帧证据的保守处理，不把批内帧数当作发射机新鲜度证明。
驱动／USB 内部字节年龄限制仍存在。

### 只读快照观察入口

原 observer 不带轴参数时继续只记录原始输入。必须同时显式提供两轴标定才能输出 Source 快照：

~~~sh
robot-control-sbus-observer --device /dev/ttyACM0 --duration-ms 45000 \
  --steering-axis 200,1000,1800,0 --throttle-axis 200,993,1800,0
~~~

四个轴字段依次为 min、center、max、reverse（0或1）。缺项、重复、越界或顺序错误在打开串口前拒绝。
此命令形态不是硬件授权；实机运行须对应独立 preflight／授权。
快照观察采用明确记录的兼容策略：通道0/2/5/6、输入死区50/1000、按钮／档位阈值500/1500、
档位30/60/100 rpm、全局100 rpm、输出死区3 rpm、恢复3批、超时100 ms、按钮冷却300 ms。
启动记录打印实际配置；策略只用于输入诊断，不能作为运动部署 profile。
工具没有 CAN 发送依赖；每批打印原始帧和相干快照，空批也执行失效判断；关闭时先输出无效快照再退出。

## 验证覆盖

| 门槛 | 覆盖 |
| --- | --- |
| V07 | 99/100/101 ms、未来接收时间、时钟倒退、过期批次、空批与重复读取；100 ms即失效 |
| V08／V06 | 恢复2/3次、一直按住、残帧、拒绝、lost/failsafe→同批健康、三帧合并最多计一次、rearm批次后续非零仍发布零 |
| V09–V13 | 独立归一化向量、959/960/1040/1041、非对称校准、反向、45/15与100/0混控；非中立但0 rpm禁止rearm |
| V14 | 299/300 ms冷却、提前按压不排队、按钮／档位阈值、3 rpm输出死区、故障绕过冷却立即撤权 |
| V15 | 同批重放、旧／新会话、UART错误后重开、零会话／回绕、sequence／授权溢出、并发值快照 |
| 配置／数值 | 缺失标定、非法通道／阈值／时间／限幅、非法事件包、INT32_MAX档位乘法 |
| PTY组合 | 显式8N2，真实Reader→Parser→Source；启动拒绝、分段帧、健康与启用、非零候选、同批故障、超时、重开、断开 |
| CLI快照模式 | 参数校验、启用首批零、非零候选、failsafe撤权、恢复不自启、新边沿rearm、非零输入下SIGTERM输出零／无效、截止shutdown |
| 历史事件回放 | 原接收时间及read边界；静态1428帧，第一轮4098帧／2拒绝／422 lost／351 failsafe，第二轮4290帧／3拒绝／420 lost／348 failsafe |

历史回放使用已独立核对 raw bytes 的 P9.2 事件日志；是离线兼容 profile 测试，不是新物理验收。
第一轮 P9.2 采集仍为 FAILED。PTY不证明电气／实际波特率；无设备目标 smoke 不证明 UART 和人工操作。

软件检查采用 Host Debug／Release、Clang ASan/UBSan、clang-format、范围内 clang-tidy、Python语法、锁定 Docker 全工程 aarch64 Debug及 ELF 审计。
最终版本含快照CLI，三组均33/33，无skip；锁定交叉构建56步骤及observer独立ELF检查通过。
observer最终哈希 e7b4cdedb9511f4b92c93fb5d6dc9b7a853afbdd851a4ab2adb1de0ea4779999。
完整证据见 [P9.3 evidence](evidence/p9_3_sbus_source_20260916/README.md)。
ASan使用 detect_leaks=0，不宣称 LSan。范围内 clang-tidy沿用 P9.2 检查族，未关闭新增检查。
最初测试夹具曾忽略恢复后冷却，并把含内部帧头的坏帧当成无残留候选；已分离夹具／修正预期。
静态检查初次指出测试辅助函数参数易互换，已改具名字段，初始报告保留。

初版纯逻辑 aarch64 程序于2026-09-16 12:35:02 +08:00在lubancat运行通过，校验哈希后10秒上限内退出0，无设备访问。
哈希74553f71720d6e65892a21d54d1de8b7ad6ab373df457e7a6553b47b2e12d820；
隔离目录 /home/cat/.cache/robot-control/staging/p93-866feaf-74553f71。
该 smoke 早于本轮新增的 stop／CLI接入，不能代替最终新生产者实机验收。

## 本轮方向／标定采集

操作员授权且确认准备后，完成一轮30秒只读采集：4284帧，flags全0，无拒绝或失连续性，独立raw解码匹配。
SIGTERM退出143；发送到确认回收约32.058 ms（包含包装器调度／日志开销），小于预定1秒上限。
提示送达均成功；操作员随后回复“确认”，确认方向动作及最终发射机开启、摇杆中立、CH6松开、CH7中档。

| 动作 | 稳定平台（raw） | 当前输入标定 |
| --- | --- | --- |
| 初始中立 | CH1=1000；CH3=992..993，中位数993 | center分别1000、993 |
| 前推／后拉到底 | CH3=1800／200 | 油门min200、max1800，不反向 |
| 右推／左推到底 | CH1=1800／200 | 转向min200、max1800，不反向 |

稳定平台用于方向／端点辨识；完整过渡数据保留，不把整个提示窗口都称为稳定。
末段回中有短暂偏移，不能把其整个区间当成静态噪声标定，也不因操作员简短确认猜测具体原因。
死区和健康策略沿用已测试的兼容值，按钮／档位离散值结合 P9.2 已验收数据；不授予任何驱动运动资格。
旧 observer 哈希仍为 bd76d3fd3d77d2ade41c4ba5e28a884fca5ab24a8b53947fd44db5e11cc03fe8。
本轮 runner 已使用，授权不复用；目标记录保留在 /home/cat/.cache/robot-control/staging/p93-calibration-bd76d3fd。

## 最终 Source 无运动验收

两轮均使用上述最终 observer 和已确认轴 profile；未发送 CAN，也未启动驱动或生产服务。
第一轮人工操作门槛未满足，按操作员要求永久保留为 **FAILED**，不合并两轮证据。
第二轮独立45秒采集通过：6434帧、6430次读取、6431份快照，2个启动候选拒绝；
独立 raw 解码及每份快照映射全部匹配，11项契约门槛通过。

- 新授权1/2/3分别出现在7.134455076、25.573039376、40.903689491秒，每次先发布零。
- 正常禁用、非中立按压拒绝、回中不自动启用均通过。
- lost/failsafe立即撤权；恢复通信后保持禁用，直到新的中立按压。
- SIGTERM将末份36/36 rpm诊断输出变为0/0、无效及shutdown，保留接收时间；退出143，信号至回收33.3009 ms，低于1秒上限。
- 操作员已确认启动前按住CH6、非中立按压、发射机关／开，以及最终开启／中立／CH6松开／CH7中档。

第二轮原分析器要求启动0–2秒全部原始CH6为高，错误拒绝了4帧旧状态特征前缀。
修正判据保留全部启动数据，要求0–3秒输出始终零／无效／授权0，且恢复后的按住禁用状态持续至少1秒；
实测合格区间0.022259177–2.997298694秒。原报告、原分析器、修订判据和负例检查说明均保留于
[GATE_REVIEW](evidence/p9_3_sbus_source_20260916/source_hil_attempt2/GATE_REVIEW.md)。
这是验收分析器修正，生产代码与采集数据未改变，第一轮仍未通过；不推断前缀的具体USB／驱动来源。
所有本轮授权与runner均已消耗，不授权未来采集。

## 复现及收口

~~~sh
cmake -S . -B out/build/p93-host-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build out/build/p93-host-debug --parallel 2
ctest --test-dir out/build/p93-host-debug --output-on-failure
# Release: p93-host-release，CMAKE_BUILD_TYPE=Release。
# Sanitizer: p93-sanitizer，clang/clang++，C/CXX_FLAGS均为
# -fsanitize=address,undefined -fno-omit-frame-pointer
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir out/build/p93-sanitizer --output-on-failure
ROBOT_CONTROL_SYSROOT="$PWD/sysroots/rk3588-ubuntu2204" \
ROBOT_CONTROL_SYSROOT_LOCK="$PWD/sysroots/rk3588-ubuntu2204.lock.json" \
ROBOT_CONTROL_PRESET=rk3588-debug ./scripts/build/build_rk3588.sh
~~~

交叉身份是基点上的dirty开发快照，不宣称已提交的clean Release构建；纯逻辑smoke同样由项目CMake与锁定sysroot构建。
新Source真实输入无运动验收及操作员确认已完成；最终提交的远端CI结果在提交后独立核对。
P8运行编排、P10系统授权衔接、P6长稳与运动权限不在本节点完成声明内。
