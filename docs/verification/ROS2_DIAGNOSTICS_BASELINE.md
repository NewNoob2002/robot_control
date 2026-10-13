# ROS2 诊断包装与授权合同基线 — 2026-10-10

用户要求先完善诊断启动/显示并定义状态年龄和运动授权合同。
已实现最小 `robot_control_ros2` 包、根CMake/独立colcon安装、`ros2 run`、单角色launch、
host-local receipt monitor。当前外部运动未接入，RCI1 probe仍不执行；
[运动v2合同](../plans/ROS2_EXTERNAL_AUTHORITY_CONTRACT.md)可审阅，但其运行时与安全矩阵尚未实现。

## 本轮验证

| 检查 | 实际结果 |
| --- | --- |
| Debug focused CTest | app_ipc_contract/control_cycle_contract/ros2_receipt_contract，3/3 |
| ASan/UBSan focused CTest | 同样3/3；原有sanitizer flags保留 |
| 锁定Humble真实DDS | 原status/probe、shutdown/context race、SIGTERM、node/server restart、paused simulation clock通过 |
| 根CMake安装+ros2 run | 非源码工作目录，缺socket、WAITING/DISCONNECTED/FRESH/STALE、500ms silence、同server恢复、node/server restart、SIGTERM通过 |
| colcon安装+ros2 launch | 同样通过；namespace=/installed、自定义绝对DDS profile、无/clock，source之外启动 |
| 纯receipt逻辑 | 499999999ns有效/500000000ns过期、重复/倒退、非法/深嵌套/超长JSON、布尔类型、无运动授权、probe error不续龄、新session/断连/时钟倒退通过 |
| locked aarch64 cross | 实际sysroot校验、根CMake configure、fixture构建/ELF审计、无ROS SDK的Python-only组件安装通过；最终安装脚本/launch与源码相同 |
| 静态与路由 | Ruff、CI routing、Bash syntax、actionlint、git diff --check通过；5个Python文件active LSP无诊断 |

## 提交前完整复跑（2026-10-12）

根CMake新增`add_subdirectory(ros2)`后，按提交前要求串行完整复跑：

- `test-app-debug`重新configure/build后完整CTest 43/43通过（新增ros2_receipt_contract），
  四项managed vcan实际执行，无skip/unavailable。
- `test-app-san`（ASan/UBSan，`ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`、
  `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`）完整43/43通过，无skip/runtime error。
- `bash scripts/test/test_ros2_ipc.sh`锁定Humble：receipt contract、原DDS、安装后ros2 run及
  colcon launch三项PASS。日志中的`XMLProfileManager ... Could not extract any profile`来自测试
  故意使用的空profile，只证明profile路径传给子进程，不证明profile内容生效。
- 复审发现本机shellcheck 0.11.0（与CI一样覆盖全部`.sh`）对`test_ros2_ipc.sh`报
  SC2155/SC2154/SC1091并退出1。该脚本由30f3b41引入（13f6a5e提交说明误写为3823e51），
  远端CI此前未运行该提交；CI所用apt shellcheck 0.8.0对原脚本是否同样失败未验证。
  已按仓库既有lock读取方式修正并校验必需字段，全部脚本shellcheck 0.11.0退出0。ruff、py_compile、receipt单测、CI scope与`git diff --check`通过。

本次未重跑locked aarch64 cross、板端或物理设备；cross证据仍为上表10-10记录，C++源码未改变。
归档`evidence/ros2_diagnostics_20261010/FILES.json`保持10-10源身份快照不改写；本次修改的
`test_ros2_ipc.sh`及三份状态文档与该快照不再一致，属预期，归档日志SHA256SUMS仍校验通过。

两个CMake文件active LSP服务未ready，未把空诊断当成通过；已实际验证host、Humble/colcon与cross configure。
没有改C/C++运行逻辑，本轮未重跑全部43项CTest或managed vcan；此前完整42/42及vcan证据保留，
此次报告只声明上述局部检查。未新增板端执行、物理CAN/UART、驱动/运动、UFW变更或远端Actions结果。

镜像仍为docker/ros2/image.lock固定Humble平台digest。运行命令：

```bash
python3 scripts/test/test_ros2_receipt.py
bash scripts/test/test_ros2_ipc.sh
ctest --test-dir out/build/test-app-debug --output-on-failure \
  -R '^(app_ipc_contract|control_cycle_contract|ros2_receipt_contract)$'
ctest --test-dir out/build/test-app-san --output-on-failure \
  -R '^(app_ipc_contract|control_cycle_contract|ros2_receipt_contract)$'
```

初次安装运行失败来自Docker `/tmp`默认noexec，ros2 run返回No executable found；
对照os.access(X_OK)/findmnt确认后，隔离测试安装tmpfs显式加exec，仍保留readonly root/network none/cap-drop。
第二次运行完成根安装回归，但colcon setup在nounset下因COLCON_CURRENT_PREFIX未设置退出；
source生成setup时临时关闭nounset后通过。失败日志保留，不报告为源码功能通过。
初次Ruff发现source/test shebang文件缺执行位，已补可执行权限，复查通过。

最终有效显示：500ms无合法新状态时healthy=false、connected=null；保留旧连接报告在
ipc_connected_reported。明确IPC断开立即显示DISCONNECTED/healthy=false。
source_age_ms=null/source_age_verified=false，绝不跨机相减monotonic时间。
monitor以100ms steady timer评估，不声称硬实时；下游仍须对monitor自身死亡做过期处理。
新session可重置诊断序号，不能据此推断具有运动防重放能力。

[证据与源身份](evidence/ros2_diagnostics_20261010/README.md)保留原失败和实际通过日志。
[使用说明](../development/ROS2_DIAGNOSTICS.md)给出安装与板端/host启动方式；新包尚未部署到板端旧stage。
下一切片是按合同先补真实owner/适配入口的运动v2回归，再实现挑战/授权；不得用独立影子模型
或单纯连通/probe_ok替代运动资格。RTK与实际零目标/运动仍按各自计划和授权推进。
