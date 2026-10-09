# 当前 diff 整理与 RK3588 IPC/ROS2 软件验证（2026-10-09）

用户要求整理当前diff后实板测试；RTK暂停。日志依赖/formatter/契约和过时文档已整理，
既有IPC fixture可交叉构建。主机Debug与ASan/UBSan各38通过、4项vcan跳过；
修复Humble SIGTERM/context竞态，确定性回归及真实DDS通过。
锁定交叉构建8个ELF审计通过，RK3588设备无访问10项检查及Humble导入/DDS两项通过。
独立用户目录部署，无残留测试进程/socket；最初使用私有运行时。
用户现已完成原生Humble ros-base安装，/opt/ros/humble导入与同一DDS回归通过；
host固定desktop镜像的RViz窗口/OpenGL/配置保存也通过。真实CAN/UART和驱动/运动HIL未执行。
产物身份、命令、原始失败、运行时来源及范围见[实板基线](IPC_BOARD_BASELINE.md)。
本次提交前逐项核对108个编译输入和部署节点/测试，与原验证产物一致；
可携带的原始测试与身份记录归档于[evidence/ipc_ros2_20261009](evidence/ipc_ros2_20261009/)。
以下记录保留各轮原始范围，旧“未部署/smoke”不代表本轮结果。

---

# spdlog / IPC 复审 P2 修复

上一轮未确认P1；本次修复两项P2，未改变硬件操作授权。

- Logger最终编码记录上限为512字节，包含模块/时间/PID/TID前缀和换行。
  超长记录尾部为`truncated=1`并保留换行；普通和节流输出使用同一实现。
  app轮转预留、长度检查和镜像缓冲区复用`Logger::max_record_bytes`。
- ROS2 IPC定时器显式使用`ClockType.STEADY_TIME`，不随use_sim_time或暂停的/clock停止。

先新增回归再修改实现：原实现的linux_platform_integration出现38条边界断言失败；
Humble测试在启用use_sim_time且不发布/clock后因收不到连接状态失败。原失败日志保留。
最终日志回归覆盖普通/详细格式的511、512、513字节边界，以及20KB context、module、
event和节流记录在4096字节非阻塞管道中的完整换行及健康计数。
Humble DDS回归覆盖普通时钟、SIGTERM、节点重启、暂停仿真时钟下的探测、服务端退出与重连。

本次实际验证：

- `cmake --build out/build/test-app-debug --parallel 4`；
  `ctest --test-dir out/build/test-app-debug --output-on-failure --no-tests=error -j 2`：
  38项通过、4项vcan跳过，无失败；不能记为完整42/42。
- 相同命令用于test-app-san，设置`ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`
  与`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`：38项通过、相同4项vcan跳过。
  跳过日志为`namespace capabilities or kernel vcan support are unavailable`。
- docker/ros2/image.lock固定Humble镜像、无设备/无外部网络环境中运行
  `python3 scripts/test/test_ros2_ipc.py out/build/ros2-ipc-humble/app/robot_control_ipc_fixture`：通过。
- control-hil-regression构建diagnostics/hil；`scripts/test/test_control_diagnostics.py`：通过。
- 固定交叉镜像身份校验和实际Ubuntu22.04 sysroot锁校验通过。
  新目录out/build/cross/spdlog-fix.jYpyW7构建app、device-config、platform-probe；
  三个ELF的解释器、依赖、符号版本和无RPATH审计通过，platform-probe的Phase3符号检查通过。
- `clang-tidy -p out/build/test-app-debug service/logging/logger.cpp app/runtime_log.cpp
  tests/integration/linux_platform_tests.cpp`退出0；工具23.1.1与既有22.1.8基线不同。
  保留既有位运算、optional-access和未使用spdlog前置声明的命名提示；不宣称无告警。
- 修改文件的主动LSP扫描未发现未处置诊断，但部分C++服务超时/无法确认clean；
  C++验证以上述编译与clang-tidy为准。主机ROS导入缺少依赖的提示局部标注，
  所有对应导入已在固定Humble镜像实际验证。JSON异常传递和精确False检查的误报局部标注，
  不改变拒绝行为。Ruff导入检查、Python语法和git diff --check通过。

日志位于out/verification/spdlog-fix/，包含原始失败、修复后的通过、静态及交叉审计结果。
未部署、未执行目标板smoke或硬件测试；远端CI未运行，不以原始阶段的42/42替代本次跳过项。

---

# 2026-09-21 ROS2 / IPC 第一阶段

基于b6cf527，在codex/ros2-control-thread-integration实现可选Unix SOCK_SEQPACKET
状态/探测端和独立Humble rclpy节点；合同见[ROS2_IPC.md](../development/ROS2_IPC.md)。
只读请求不会填充external或授予运动权限。每周期最多处理一包，单客户端、同UID、
私有目录、年龄/序号/范围校验和慢观察者断开；默认关闭，旧配置继续可用。

实际验证：
- cmake --build out/build/test-app-debug -j 4；ctest --test-dir out/build/test-app-debug
  --output-on-failure -j 2：42/42，无skip。初始化错误路径的errno修正后定向配置/IPC复核2/2。
- cmake --build out/build/test-app-san -j 4；ASAN_OPTIONS=detect_leaks=0:halt_on_error=1
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir out/build/test-app-san
  --output-on-failure -j 2：最终代码42/42，无skip。
- 新增app_ipc_contract；app vcan从23扩为25场景，验证非零探测不产生运动和观察者堵塞不影响SBUS。
- docker/ros2/image.lock锁定Humble镜像内，根CMake构建robot_control_ipc_fixture，
  scripts/test/test_ros2_ipc.py通过真实DDS状态/探测、SIGTERM、节点重启、服务端退出与重连。
- 四个相关TU的clang-tidy通过；仅保留platform/linux/error.hpp既有optional-access提示。
- 锁定交叉镜像GCC11.4、经锁校验的实际Ubuntu22.04 sysroot，构建
  out/build/cross/ros2-ipc-20260921；app/device-config两个ELF的解释器、依赖、符号版本及无RPATH审计通过。

首次IPC测试被沙箱禁止bind，后在授权的本地socket环境通过；首次新增vcan测试未处理
app退出时的ECONNRESET，修正测试后完整回归通过；首次静态检查报告errno错误路径，已修正。
本地日志：out/build/test-app-debug/ipc-tests.log、ipc-static.log，
out/build/test-app-san/ipc-tests.log，out/build/ipc-cross.log。
未部署、未运行RK3588设备无访问smoke或硬件测试。外部运动、安全授权合同、Twist转换及
ROS2目标环境仍未交付；新版本不继承旧实车验收。未提交/push，远端CI不作为本轮证据。

---

# 2026-09-21 源码交接复审与文档同步

README、AGENTS当前检查点、TEST_APP_HANDOFF和ROS2移植说明已统一到当前两阶段配置、
通电零速待机、±5rpm故障阈值与±2rpm静止确认；旧地面检查点加后续入口，历史证据不改写。
复审覆盖配置解析、单一控制者/每事件抑制、发送门控、设备参数保存、日志子进程及退出恢复。
旧版run-5475日志已确认是零目标待机右轮−2.1rpm触发uncommanded，距最后非零目标18.57秒；
该证据不能证明手推或测速错误，也不代表新版本硬件验收。

发现并处理：
- 工作区rk3588-release误开Debug-only app且硬编码源码树sysroot；恢复原预设，
  锁定GCC11.4容器使用实际目标sysroot执行Release配置通过；不宣称完成Release交叉编译。
- CI补齐robot-control-device-config、device_parameter_gates与device_config_vcan_managed，
  Debug/sanitizer均要求执行且不能skip。
- 两个app/device vcan测试共用/tmp接口锁，增加CTest RESOURCE_LOCK；
  `ctest --test-dir out/build/test-app-debug -R '^(test_app_vcan_managed|device_config_vcan_managed)$' -j 2 --output-on-failure`
  自动串行、2/2通过（app23场景、设备15场景）。

现有Debug41项、ASan/UBSan41项、共享HIL5项及交叉ELF证据沿用下节，
本轮CMake重新生成后ninja无待构建目标。CI路由回归、修改Python语法、启动脚本bash语法、
Markdown相对链接和git diff --check通过；本机未安装shellcheck，交由CI现有步骤执行。
未发现剩余阻塞项；硬件、内核延迟TX、速度比例与生产验收限制仍保留。
源码提交排除本地.clangd偏好与根目录旧robot-dev压缩包，均保留原位。
远端CI结果以对应提交的GitHub Actions为准，不以旧0f07dbc的绿色状态代替。

---

# 2026-09-21 待机反馈±5rpm及对象字典十六进制日志

用户针对run-5475明确将待机/未命令轮反馈故障阈值从±2rpm提高至±5rpm。
MotionMonitor仅在该阶段超过±50个0.1rpm时锁存uncommanded，恰好±5rpm允许；
超过±2rpm仍重置静止保持。启动预检、重新授权、停车与清理的±2rpm静止确认、
1秒停车期限、150ms稳定保持、方向保护和反馈硬限不变。旧运行失败记录保留。

共享ControlQualification的hil_read、基线不匹配详情及设备参数日志使用0x前缀十六进制；
index/sub/raw分别固定4/2/8位，保留原始无符号位模式，不改变速度/时间诊断或trace格式。

验证：
- 先更新边界回归，旧实现出现4项失败；修改后通过，覆盖双轴±5.0/±5.1rpm及run-5475反馈。
- `cmake --build out/build/test-app-debug -j 2`通过；Debug全部41项分组通过。
  首次沙箱日志子进程启动失败；提权测试时并行app/device vcan争用接口锁导致新增日志断言失败，
  单独重跑app完整23场景通过。其余40项通过，不将首次失败记作通过。
- `cmake --build out/build/test-app-san -j 2`及
  `ASAN_OPTIONS=detect_leaks=0 ctest --test-dir out/build/test-app-san --output-on-failure`：41/41，无跳过。
- control-hil-regression构建及trace/motion_gate/zero_gate/help/control_qualification_vcan_managed：5/5。
- clang-format及git diff --check通过；3个相关翻译单元clang-tidy无新增警告，保留error.hpp既有optional警告。
- 锁定Docker/GCC11.4、实际Ubuntu22.04目标sysroot交叉构建及两个ELF审计通过。
  构建/本地交付目录后缀app-idle5-hex-20260921；app SHA256：
  `54bd733c21dbaaa83791ebc9fd8f2fceef879d729328ff867c1eef000d5a689a`。

未部署，未进行目标smoke或硬件测试，未改变驱动器参数。

---

# 2026-09-21 通电零速保持与停车观察窗口

用户确认目标：遥控器未开机、CH6未授权/关闭、使能后中立时，app运行期间保持零速伺服。
机械抱闸存在，但本轮未确认接线、不控制抱闸，不将软件保持等同于断电/故障驻车保证。

只读SSH取得run-4769-m9qRcp的application/diagnostics及原始trace；未进行硬件发送。
trace SHA256=f5dc1d01a1196201dd22b3bd7c46af943835dbc17d68438a3274a7d0afa50d89。
该次退出前有正常正反转；成功发送6040=15、双轮零目标后280.60052ms，
反馈左21、右19（0.1rpm）触发monitor_fault=2。此前约150ms带内保持提前结束了停车阶段，
50ms后左2.1rpm被归为uncommanded。此事实不证明该反馈等于真实机械移动，也不解释此前CAN异常。

实现：
- app专用powered_standby默认开启；共享库及旧资格测试默认关闭。SafetyManager先处理故障/急停，
  无行驶授权时选择standby_hold。RuntimePolicy只接受零目标，最长1秒建立6→7→15零速伺服。
- 该路径不授予motion authorization；CH6关闭、SBUS失效撤销行驶权限，健康驱动器持续15+零目标。
  遥控恢复仍需中立和新CH6边沿。新授权保持原有150ms静止等待及新TPDO确认，不插入Shutdown解轴。
  新授权后在新TPDO到来前偏转摇杆仍输出零；过期/重放/超限及故障保护继续生效。
- 保持状态丢失、过期反馈、驱动器故障/急停/代次变更不自动重新建立保持；不执行故障复位。
  UART读错误、CAN失败、信号退出及清理仍走原流程，不能承诺这些路径保持力矩。
- MotionMonitor保留完整1秒停车观察窗口，早期150ms带内保持后再跳出则重新累计稳定时间，
  不延长原期限。待机±2rpm、反馈硬限及新鲜度未放宽。先新增回归复现旧实现失败，再修改。
- 文档说明通电待机与行驶授权区别，日志增加servo_enabled和zero_hold。协议状态不是力矩证明。

实际验证：
- Debug完整41/41；最后的授权新TPDO边界修改后，定向4/4再次通过（含四个app vcan场景）。
- 最终代码ASan/UBSan完整41/41，ASAN_OPTIONS=detect_leaks=0。
- app独立vcan覆盖23场景；新增无遥控、未授权保持，断言CH6关闭/遥控失联持续15+零目标，
  原有配置/日志/信号/超速/通信保护回归保留。peer不证明实际力矩。
- 旧资格入口共享回归5/5；scoped clang-tidy无新增诊断，保留error.hpp三条既有optional提示。
- 锁定Docker/GCC11.4、实际Ubuntu22.04目标sysroot交叉构建及两个ELF兼容性检查通过。
  构建目录out/build/cross/app-powered-standby-20260921；交付out/handoff/app-powered-standby-20260921。
  app SHA256=144a815baa4eff3a5e0c5a67f33637b846c9623453c56273d63264316a7ec09b。

未部署、未做本轮目标smoke/运动/坡道试验、未写EEPROM。保持实际力矩、抱闸联动和坡道能力待验证。
现有现场.conf可保留，运行app的新策略不要求持久参数写入；上电到app预检完成前不在软件保持范围。
若另需上电锁轴，应按设备配置阶段配置2007:00=1、保存/重启并独立验证，而不是将keep解释为开启。
历史失败及下列历史验证范围保持不变。

---

# 2026-09-21 驱动参数分区及设备配置扩展

共享.conf按注释分区为驱动器参数和app参数，保持原key=value语法。
新增驱动最高转速、左右加速/减速/急停时间、I/O停止方式、上电锁轴、驻车及方向；
速度模式3、同步1、quick-stop5受当前profile约束。可选keep不读写、不比对，旧文件缺省新项为keep。
app max_rpm不得超过明确填写的drive_max_rpm。分辨率继续1..10，实际打包速度倍率尚待验证。
设备工具读齐选定参数后才开始写入，每项修改及保存前都读回校验。
drive_pdo_profile=1建立固定app启动基线，临时心跳/Pre-operational确认后写PDO，保存前恢复心跳0；
=0只核对已有布局。保存仍为单次2010:00 U16=1，覆盖设备全部RW参数，成功提示人工重启。
app仅只读比较选定持久参数；原临时运行PDO/心跳/watchdog流程及退出恢复保持。
不写CAN节点/波特率、极对数、编码器、电流、PID或制动电阻。

验证：
- 配置新字段回归先在旧解析器失败，扩展后通过；包含范围、keep、I/O字节及跨字段速度限制。
- Debug41/41分组通过：新增配置/门控、设备vcan、app vcan及其余37项。
- ASan/UBSan41/41，ASAN_OPTIONS=detect_leaks=0。
- 设备vcan15场景：多参数成功、PDO基线重建、无需修改、keep保留、脏基线/运动/使能拒绝、
  不支持对象、写入abort/读回不一致/应答丢失、PDO写失败、保存abort/应答丢失、节点boot。
- app peer禁止所有新增持久参数写入，新增加速参数不匹配预检退出场景。
- 独立门控测试：运行app拒绝所有持久参数及EEPROM写入；设备白名单拒绝控制字/目标/节点/
  波特率/恢复出厂、错误子索引/宽度和越界值。
- 共享旧HIL五项回归通过；不改变旧qualification参数访问范围。
- scoped clang-tidy无新增诊断；保留error.hpp optional与qualification.cpp既有性能提示。
- clang-format及git diff --check通过；锁定Docker/GCC11.4+真实RK3588 sysroot交叉构建，两个ELF检查通过。

未部署、未运行本轮实车、未写EEPROM或重启设备。
保存重启后的分辨率生效/打包目标倍率仍未确认，run-3095速度异常不宣称已修复。
示例参数不是现场实测；替换程序/配置前核对现场值，可选keep用于保留原值。

---

# 2026-09-21 设备配置/运行两阶段

设备工具robot-control-device-config复用外部rk3588-test.conf；默认只校验文件，--apply才执行。
持久参数白名单目前仅speed_resolution(0x2026:05)。禁用/零目标/静止及正常profile预检后，
必要时写入并读回；最后单次发送0x2010:00 U16=1保存所有RW参数，提示人工重启。
不执行NMT/RPDO、使能或自动重启，不自动重试保存，不自动回滚参数。
app仅比较期望分辨率，启动前不匹配即拒绝；发送门控禁止分辨率和EEPROM写入，退出不恢复分辨率。
临时PDO/心跳/watchdog初始化和清理继续使用原流程。两程序共享接口锁。

验证：
- 新增运行阶段只读断言先捕获旧版分辨率写入，修改后通过。
- Debug40/40分组通过：app vcan、device vcan以及其余38项。
- ASan/UBSan40/40，ASAN_OPTIONS=detect_leaks=0。
- 设备vcan覆盖11个场景：修改成功、值已相同、基线异常、移动、使能、写入abort、
  读回不一致、写入已生效应答丢失、保存abort、保存应答丢失、节点boot；失败不重试/不报成功。
- 共享旧HIL五项回归通过：控制周期、运行策略、两个发送门控、初始化恢复vcan。
- 受影响TU clang-tidy完成，无新增诊断；保留原qualification性能提示及error.hpp optional提示。
- 格式及git diff --check通过。
- 锁定Docker/GCC11.4+真实RK3588 Ubuntu22.04 sysroot交叉构建，两程序ELF依赖/符号版本检查通过。

未部署、未操作实车EEPROM、未重启设备、未进行本轮实车运动。
run-3095的速度比例异常未闭环；保存重启生效仅为待验证解释，不宣称已修复机械行为。
app无法仅凭SDO读回判断设备是否实际重启过，也无法据此证明0x60FF:03倍率正确。
当前交付不包含用户目标机现有配置；替换程序时保留并核对现场.conf。

---

# 2026-09-21 CH7三档更新

在上一版分辨率/日志功能基础上，将startup.cpp的gear_rpm改为max_rpm的30%/60%/100%，
整数向下取整、配置值至少1rpm；Source通道识别、授权、混合和3rpm死区不变。
max_rpm=25时为7/15/25rpm；默认max_rpm=5时低档1rpm被死区清零。
--check-config和config.conf同时显示gear_rpm及gear_full_throttle_rpm。

本轮修改：application/config/startup.cpp、scripts/test/test_startup_config.py、
tests/integration/test_app_vcan.py及TEST_APP/ROS2_PORTING_GUIDE说明。
上一轮修改范围：配置与.conf的speed_resolution；ControlQualification及qualification门控的
U16 SDO初始化/读回/恢复；RuntimeConfig/RuntimeLayoutProof的分辨率绑定；
RuntimePolicy编码和app IoContext发送限幅；RuntimeLog及service/logging的EasyLogger终端镜像。

验证：Debug39/39、ASan/UBSan39/39（detect_leaks=0），包含CH7=500/501/1499/1500实际切档、
max_rpm=25和speed_resolution=10发送70/150/250，及原有故障矩阵。
startup配置回归先失败，修改后通过；静态检查无项目诊断、格式/diff检查通过。
锁定Docker+真实目标sysroot交叉编译及ELF检查通过。旧HIL本轮未改，不重复上轮5/5验证。
app SHA256：32a7523fa5fc29b312494e63e63ada29d12d23efaf00f8111f883f899a6bceea。
交接包：out/handoff/app-gears-20260921/robot-control-rk3588-test-app.tar.gz。
未部署或实车验证；新的三档行为需替换二进制后再验证。旧交接包保留，不代表当前产物。

---

# 2026-09-21：速度分辨率与终端日志交付

- 新配置speed_resolution=1..10（默认1），控制0x2026:05 U16；原值先读回验证，易失写入并读回，
  停车后恢复原值。应答丢失仍承担恢复责任，恢复失败明确保留错误。
- 逻辑rpm按N倍编码为RPDO原始计数；反馈维持0.1rpm；配置与读回proof必须一致。
  旧HIL默认不访问该对象，门控仅在TEST_APP构建开放。
- application.log与diagnostics.log统一EasyLogger格式，附日期时间/PID/TID，并非阻塞同步到终端。
  终端丢镜像计数见console_summary；磁盘/收集器故障仍触发保护。
- 保留用户工作区max_rpm解析1..100、反馈硬限±100rpm的修改；
  现有3rpm死区使完整配置实际接受3..100。该范围不代表实车验收。
- Debug39/39、ASan/UBSan39/39（detect_leaks=0）、原HIL初始化/恢复/门控5/5通过。
  vcan包含分辨率2/10、非法原值、SDO拒绝、已写入但应答丢失、读回不一致、恢复失败、
  终端堵塞与文件/终端逐行一致性；纯策略覆盖1..10和±1000rpm编码边界。
- 静态检查无错误；保留qualification.cpp及platform/linux/error.hpp既有性能/optional提示。
  格式与diff检查通过。锁定Docker+真实Ubuntu22.04 sysroot交叉构建及ELF审计通过。
  97个冻结项目源码/头文件/CMake文件与当前实现一致，见SOURCE_INPUTS.json。
- 本地包：out/handoff/app-20260921/robot-control-rk3588-test-app.tar.gz。
  app SHA256：727f97cd49a75c65cf265d3276270342b262f567b7f3617e48fef9ac634a6eaa。
- 未部署、未执行本次RK3588smoke或实车验证，真实分辨率比例、生效及恢复仍待验证。
  当前修改尚未提交/push；2026-09-20旧CI不覆盖本次代码。

命令与JUnit在同目录debug.log/debug.xml、sanitizer.log/sanitizer.xml、hil.log/hil.xml，
静态与交叉日志为static.log/cross.log，构建身份见BUILD.json。

---

以下是2026-09-20历史交付记录，保留原始版本与验证范围。

# SBUS 整车 app 软件交接基线（2026-09-20）

基于已推送的0f07dbc/checkpoint/ground-functions-20260920；本次app修改尚未提交/push，旧CI不覆盖新入口。
入口及操作说明见 ../development/TEST_APP.md。IPC暂停；本次无部署、硬件操作或生产验收。

## 产物

本地交接目录 out/handoff/app/rk3588，压缩包 out/handoff/app/robot-control-rk3588-test-app.tar.gz。
包内 SHA256SUMS、BUILD.json、VALIDATION.md 记录身份和验证；独立源快照归档在同级目录。
app SHA256: e08556061d776e96f90046d0d312696c2279ccc1d77dc8e8648ae5ac24d7af01。
冻结源码 out/test-app-v2-cross-source，快照哈希 f120de33a7dbf64bad7ffbcbfe638ac9088409bb1f23bc37f961fad07d8fd805。
锁定Docker镜像和真实Ubuntu22.04 sysroot见 BUILD.json；87个项目编译输入与工作区逐字节一致。
冻结后只调整了host监控测试的格式及交接文档，不影响ARM编译输入。

## 验证

- cmake --build out/build/test-app-debug --parallel 2；ctest --test-dir out/build/test-app-debug --output-on-failure：39/39。
- cmake --build out/build/test-app-san --parallel 2；ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 ctest --test-dir out/build/test-app-san --output-on-failure：39/39，无skip。
- vcan覆盖基线拒绝、接口选择、持续正反/转向、CH6切换、无限时SIGTERM/SIGHUP、SBUS/CAN失联、超速、日志退出/停滞、第二进程锁及轮转。
- 原HIL六项回归6/6，原runtime control_loop_vcan_managed 1/1；日志在 out/handoff/app/hil-regressions.log 和 runtime-regression.log。
- 最后极值回归先复现错误，修正后通过；仅改变观测极值初始化，不改变运动策略。
- 最终sanitizer第一次与Debug并行，因共享/tmp接口锁在interface场景失败；原始失败XML/log保留，串行全量重跑通过。并行跨构建测试须避免同名接口锁冲突。
- clang-tidy22.1.8受影响TU通过，仅保留既有platform/linux/error.hpp optional-access提示；clang-format检查通过。
- python3 out/handoff/app/cross-build.py：锁定离线交叉构建通过；audit_qualification_elf.sh通过，动态加载器、sysroot符号版本、依赖和无RPATH检查通过。
- actionlint、CI scope检查、bash -n、依赖来源检查及既有P10.3证据检查通过。shellcheck不可用。

最终XML为 out/handoff/app/debug-v2.xml、sanitizer-v2.xml；静态日志static-v2.log、交叉日志cross-v2.log、ELF日志elf-v2.log。

## 未验证项

没有新app的RK3588运行/设备无访问smoke或实车验证；没有qemu-aarch64。连续控制和200ms启动方向瞬态保护是新入口行为，旧HIL验收不可替代。
故障注入/带载/生产验收及内核延迟TX问题未因此关闭。CAN波特率、restart-ms和link-up由系统管理，app仅选择并检查接口。
