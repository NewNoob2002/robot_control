# 手动复测 IPC 与 ROS topic

新ROS包支持安装后的`ros2 run/launch`与500ms过期monitor，见
[诊断包使用说明](ROS2_DIAGNOSTICS.md)。下面保留旧板端stage的已验证直接脚本/echo方法；
新包尚未部署到该stage。monitor可替代host echo，同一固定UDP profile只运行一个参与者。

当前方法使用板端真实Unix socket和真实DDS/UDP，状态由device-free fixture合成；
不启动robot-control-app，不打开物理CAN/UART，不证明电机反馈/运动。
2026-10-10用户在聊天中报告“测试结果通过”，没有附状态日志/截图，因此记为人工报告，
不新增精确过期/恢复/性能验收。自动证据见[限定UDP复测](../verification/evidence/ipc_udp_allow_20261010/README.md)。

当前端点：board192.168.110.161，host wlan0192.168.110.198；主机限定UFW规则已添加。
地址/接口改变时先审查XML与规则目标，不复用失配地址。固定host ports54710/54711
只供一个参与者，echo/验证观察器二选一，不同时运行topic list/多个echo或使用相同profile的daemon。

## 板端终端A：状态节点

独立Bash加载原生Humble，并使用系统Python3.10，避免zsh或虚拟Python缺少rclpy。
不需要pip安装rclpy。

```sh
cd /home/cat/robot-control-tests/ipc-link-verification-20261009

env -u PYTHONPATH -u LD_LIBRARY_PATH \
    -u AMENT_PREFIX_PATH -u COLCON_PREFIX_PATH \
    /bin/bash --noprofile --norc -c '
  source /opt/ros/humble/setup.bash || exit 1
  export ROS_DOMAIN_ID=186
  export ROS_LOCALHOST_ONLY=0
  export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
  export FASTRTPS_DEFAULT_PROFILES_FILE="$PWD/board-udp-current.xml"
  exec /usr/bin/python3 robot_control_ipc_node.py --ros-args \
    -p socket_path:="$PWD/control.sock" \
    -r __node:=ipc_verify -r __ns:=/verification
'
```

节点无终端输出正常。fixture尚未启动时，topic的connected=false是预期。

## 主机终端：订阅状态

在项目根目录执行。每行反斜杠必须是最后一个字符，其后不能有空格或插入空行。

```sh
cd /home/gtc/Desktop/workspace/Linux_PROJ/robot_control

docker run --rm --pull=never --network host \
  --read-only --tmpfs /tmp:rw,nosuid,nodev --cap-drop ALL \
  --env ROS_HOME=/tmp/ros \
  --env ROS_DOMAIN_ID=186 \
  --env ROS_LOCALHOST_ONLY=0 \
  --env RMW_IMPLEMENTATION=rmw_fastrtps_cpp \
  --env FASTRTPS_DEFAULT_PROFILES_FILE=/workspace/docs/verification/evidence/ipc_udp_allow_20261010/host-udp.xml \
  --mount "type=bind,source=$PWD,target=/workspace,readonly" \
  docker.io/library/ros:humble-ros-base-jammy@sha256:ba0391e31ffcfc5a1014578ecb0f4ca438be9d624b6375c85576ae16bc5232e6 \
  ros2 topic echo /verification/ipc_verify/status std_msgs/msg/String \
  --qos-reliability best_effort --qos-durability volatile \
  --no-daemon --full-length
```

镜像须已存在；本命令不自动pull。topic echo只显示收到的内容，不自行把最后一条healthy标记过期。
500ms过期/进度检查使用scripts/test/test_ros2_ipc_remote_status.py，以host接收时钟判断；
不能把board captured_ns从host CLOCK_MONOTONIC相减。topic probe必须在板端同一时钟域采样，
本轮不从主机发送probe。

## 板端终端B：fixture

```sh
cd /home/cat/robot-control-tests/ipc-link-verification-20261009
./robot_control_ipc_fixture "$PWD/control.sock"
```

显示READY后运行8秒，再正常退出。服务每轮增加cycles并处理节点查询，约1ms休眠；
实际JSON经节点20ms timer请求/接收，不能把fixture循环频率当作topic发布率或实时保证。
应看到connected=true、sequence/cycles推进、左右目标0、executed=false、motion_authority=false。
left_feedback_tenths_rpm=-2即−0.2rpm，是固定合成值。

`$PWD/control.sock`由shell展开为绝对路径。Server通过AF_UNIX/SOCK_SEQPACKET bind创建
文件系统入口，0600权限，父目录为同UID/0700；数据由内核传递，不保存在普通文件中。
不要touch预创建它。它仅在fixture运行时可见，正常析构确认inode归属后自动删除。
8秒内在另一个板端终端执行ls -l该绝对路径，开头应为s；结束后路径不存在正常。

服务退出后观察connected=false，再执行一次fixture，观察server_started_ns改变和状态恢复。
新session的sequence重置正常。Ctrl-C退出节点再重启可观察ROS进程退出/恢复；
最后Ctrl-C停止主机订阅与节点，fixture自动到时退出。SIGKILL/崩溃可能留下路径，
须确认原实例已结束再处理残留，不能直接删除活跃socket。

fixture只有8秒，不用断Wi-Fi来判网络恢复，避免中断恰逢fixture自行退出的因果混淆。
真实app通电静止、真实SBUS接收和运动复测见TEST_APP.md及相应试验准备，另行确认具体
目标/产物/现场授权；旧runner均不因此获得新许可。
