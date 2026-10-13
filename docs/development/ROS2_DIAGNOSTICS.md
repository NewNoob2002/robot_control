# ROS2 诊断包：安装、启动与过期显示

包名 `robot_control_ros2`，只包含Python bridge、monitor和一份launch文件。
不启动app/CAN/UART、不获取运动权限；`probe_ok`是非执行校验。
使用系统Python和Humble已有的rclpy/std_msgs/launch/launch_ros，不增加pip依赖。
项目未声明整体源码许可证，package.xml使用`LicenseRef-Project-Undeclared`标记，
maintainer邮箱为显式占位；发布包前需由维护者填写，未擅自替项目选择许可证。

## 安装方式A：根CMake

原有Debug test-app构建仍是核心构建权威。ROS安装component无需ROS SDK即可安装脚本/元数据：

```bash
cmake -S . -B out/build/test-app-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DROBOT_CONTROL_BUILD_TEST_APP=ON
cmake --build out/build/test-app-debug --target robot_control_ipc_fixture --parallel 2
cmake --install out/build/test-app-debug --component ros2 --prefix "$PWD/out/ros2-install"

# 在已安装Humble的Bash环境；宿主非Humble时使用下面的锁定Docker方法。
source /opt/ros/humble/setup.bash
export AMENT_PREFIX_PATH="$PWD/out/ros2-install:$AMENT_PREFIX_PATH"
ros2 pkg executables robot_control_ros2
```

根安装无需colcon/setup脚本，显式加入AMENT_PREFIX_PATH；从任意目录可运行两个入口。
原有完整app install保留顶层 `robot_control_ipc_node.py`兼容旧部署，同一份源文件也安装到ROS索引。
交叉构建不导入目标ROS或主机Python库；运行ROS脚本仍需目标原生Humble和系统Python。

## 安装方式B：colcon只构建适配包

从仓库根目录，在原生Humble Bash内：

```bash
source /opt/ros/humble/setup.bash
colcon build --base-paths ros2 --packages-select robot_control_ros2 \
  --build-base out/ros2-build --install-base out/ros2-colcon-install \
  --executor sequential
source out/ros2-colcon-install/local_setup.bash
ros2 pkg executables robot_control_ros2
```

不对整个仓库执行无约束colcon扫描；`ros2/CMakeLists.txt`独立构建时使用ament_cmake，
只安装适配脚本与launch，不重复构建核心。ament/colcon生成的setup不支持`set -u`，
脚本中source前暂时关闭nounset，source后再恢复。

宿主没有原生Humble时，使用已有锁定镜像构建ROS安装前缀（无网络、无设备）：

```bash
source docker/ros2/image.lock
docker run --rm --pull=never --platform "$platform" \
  --user "$(id -u):$(id -g)" --network none --read-only \
  --tmpfs /tmp:rw,nosuid,nodev,exec --cap-drop ALL \
  --env ROS_HOME=/tmp/ros --env PYTHONDONTWRITEBYTECODE=1 \
  --mount "type=bind,source=$PWD,target=/workspace" --workdir /workspace \
  "$upstream_image" bash -euc '
    colcon --log-base out/ros2-log build --base-paths ros2 \
      --build-base out/ros2-build --install-base out/ros2-colcon-install --executor sequential
  '
```

## 同机无设备手动检查

以下每个终端先加载同一个Humble和安装前缀。domain与网络隔离按当前测试配置设置，
同机可用默认DDS；无 `/clock` 时仍运行。socket父目录需同UID所有、0700；不要touch socket。

```bash
# 终端A：bridge，不启动硬件。fixture启动前它报告connected=false。
ros2 run robot_control_ros2 robot_control_ipc_node --ros-args \
  -p socket_path:=/tmp/robot-control-ipc/control.sock

# 终端B：monitor。显示WAITING/FRESH/DISCONNECTED/STALE/INVALID。
ros2 run robot_control_ros2 robot_control_ipc_monitor --ros-args \
  -p status_topic:=/robot_control_ipc/status

# 终端C：根CMake编译的同ABI fixture，运行8秒后正常退出。
mkdir -m 700 /tmp/robot-control-ipc
out/build/test-app-debug/app/robot_control_ipc_fixture /tmp/robot-control-ipc/control.sock
```

正常应看到DISCONNECTED→FRESH→DISCONNECTED；再次启动fixture看到新实例/新session恢复。
fixture固定左反馈−0.2rpm、目标0，属于合成状态，不能据此验收真实反馈或车轮运动。
bridge退出时fixture独立继续；monitor在最后合法状态接收500ms后标记STALE，healthy=false。
Ctrl-C/SIGTERM可退出两个ROS入口。不要为测试过期直接断板端Wi-Fi，避免影响SSH或混淆fixture到时退出。

## launch与跨机配置

每次launch只启动**一个**DDS participant；板端选bridge，host选monitor。
`socket_path`仅bridge必填，绝对路径；`status_topic`仅monitor使用。
`namespace`改变本participant的节点/topic namespace；monitor默认相对topic会跟随namespace，
跨命名空间订阅时传绝对status_topic。`dds_profile`为空则继承环境，指定时必须是可读绝对文件路径。

```bash
# 板端：先设置ROS_DOMAIN_ID=186、ROS_LOCALHOST_ONLY=0、RMW_IMPLEMENTATION=rmw_fastrtps_cpp。
ros2 launch robot_control_ros2 diagnostics.launch.py role:=bridge \
  socket_path:=/absolute/private/control.sock namespace:=/verification \
  dds_profile:=/absolute/board-udp-current.xml

# 主机：相同domain/RMW；需安装本轮新增的包，再启动下面的monitor。
ros2 launch robot_control_ros2 diagnostics.launch.py role:=monitor \
  status_topic:=/verification/robot_control_ipc/status \
  dds_profile:=/absolute/host-udp.xml
```

`use_sim_time:=true`可验证暂停仿真时钟，IPC deadline、重连和monitor过期仍用steady clock。
节点原有`ros2 run ... --ros-args -r __node:=... -r __ns:=...`也可使用。
固定UDP profile仅允许已验证的一对参与者；host monitor替换echo/旧observer，
不并行启动相同固定端口的echo、list、daemon或第二个monitor。
launch文件只把DDS profile传给本次子进程，不改系统网络/防火墙。
历史UFW规则/网络证据见[手动复测](IPC_MANUAL_TEST.md)，本轮没有新板端部署或规则修改。

宿主CachyOS可将上面Docker构建产物用于锁定Humble容器内的monitor：

```bash
source docker/ros2/image.lock
docker run --rm --pull=never --network host --read-only \
  --tmpfs /tmp:rw,nosuid,nodev --cap-drop ALL --env ROS_HOME=/tmp/ros \
  --env ROS_DOMAIN_ID=186 --env ROS_LOCALHOST_ONLY=0 --env RMW_IMPLEMENTATION=rmw_fastrtps_cpp \
  --mount "type=bind,source=$PWD,target=/workspace,readonly" \
  "$upstream_image" bash -ec '
    source /workspace/out/ros2-colcon-install/local_setup.bash
    exec ros2 launch robot_control_ros2 diagnostics.launch.py role:=monitor \
      status_topic:=/verification/ipc_verify/status \
      dds_profile:=/workspace/docs/verification/evidence/ipc_udp_allow_20261010/host-udp.xml
  '
```

这里订阅旧板端已验证命名 `/verification/ipc_verify/status`；使用新launch默认bridge名时改成
`/verification/robot_control_ipc/status`。包仍需新构建；旧板端已归档stage没有新增launch/monitor。

## 显示合同与自动检查

monitor订阅String状态，QoS BEST_EFFORT/VOLATILE/depth1；发布 `~/diagnostics`（同QoS）10Hz，
只在状态切换或每秒输出一次日志。`receipt_age_ms`是最终monitor本机接收年龄；
`source_age_ms=null/source_age_verified=false`，不会跨机器相减monotonic时钟。
`connected`在STALE/WAITING/INVALID时为null，最近板端报告另保留在`ipc_connected_reported`；
`healthy`仅FRESH且原状态healthy时成立。500ms为逻辑期限，100ms评估加上实际调度延迟决定显示时刻。
相同session的重复/倒退sequence不刷新年龄；这不是跨重启/恶意发布者鉴权。
诊断probe错误不刷新快照。monitor自身死亡时，下游必须自行过期，不能无限使用最后一条diagnostics。

```bash
python3 scripts/test/test_ros2_receipt.py
bash scripts/test/test_ros2_ipc.sh
```

锁定镜像脚本验证原DDS status/probe、根CMake安装后的ros2 run、colcon安装后的ros2 launch，
都从非源码工作目录运行；覆盖缺socket、500ms silence、同server恢复、桥接/服务重启、
SIGTERM、paused `/clock`。fixture始终不访问物理设备。
未来运动与年龄的完整合同见[授权合同](../plans/ROS2_EXTERNAL_AUTHORITY_CONTRACT.md)。
