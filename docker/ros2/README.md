# ROS2 Humble development image

This is a host **amd64 / Ubuntu 22.04 / ROS2 Humble ros-base** environment.
It reuses the official image by immutable platform digest; no custom Dockerfile
or duplicate package installation is needed. It is independent of the core
cross-build runner and does not implement the Phase 13 ROS2 adapter.

The native binaries built here are amd64, not RK3588 aarch64 artifacts.
Use the existing cross runner with the real target sysroot for the core.
Target ROS2 deployment and DDS/robot integration remain separate acceptance gates.
RViz/Gazebo and a desktop image are outside this CLI development environment.
For RViz, a separate digest-pinned Humble desktop image and
[run-rviz.sh](run-rviz.sh) are now available; the original ros-base image/lock remain unchanged.

## Host RViz and board ros-base

From a local graphical desktop terminal at the repository root:

```bash
bash docker/ros2/run-rviz.sh
# Inspect the board's topics with the same network/domain settings:
bash docker/ros2/run-rviz.sh ros2 topic list --no-daemon
bash docker/ros2/run-rviz.sh ros2 topic echo /robot_control_ipc/status std_msgs/msg/String \
  --qos-reliability best_effort --no-daemon
```

The script uses `osrf/ros:humble-desktop` by immutable amd64 platform digest
`sha256:fb07245b32187d74350be25323d8ad2f8ca5c25c325759911a1eff2267a49c1e`.
It forwards the current desktop X11 cookie read-only through XWayland without
changing xhost access policy. It uses the host UID/GID, read-only container root,
software OpenGL, a writable `out/rviz-config` directory, and host networking.
No physical CAN/UART or GPU device is passed. HOME/USER/LOGNAME retain the host
values; only the RViz configuration subtree is mounted, not the host home tree.
The workspace is mounted at `/workspace`, so custom RViz configs can also be saved there.
Do not launch this GUI script from an ordinary headless SSH shell.

Actual host validation created a window and an OpenGL4.5 context, wrote
`persistent_settings` as the host user, and exited on bounded SIGINT.
The first GUI attempt's config-path error is preserved; the final attempt has no
RViz error. Logs: `out/verification/ipc-board-diff/rviz-smoke*.log`.
Observed packages: ros-humble-rviz2 `11.2.28-1jammy.20260804.222726`, desktop
`0.10.0-1jammy.20260804.223343`. Package licenses are inside the pinned image under
`/opt/ros/humble/share/*/package.xml` and `/usr/share/doc/*/copyright`.
This is a GUI smoke, not a robot visualization or cross-machine DDS acceptance.

For board-to-host communication, both sides use Humble, matching message types,
`ROS_DOMAIN_ID=186`, `ROS_LOCALHOST_ONLY=0`, and `RMW_IMPLEMENTATION=rmw_fastrtps_cpp`.
The board and host must have bidirectional DDS reachability; host networking removes
container NAT but does not fix Wi-Fi isolation, firewall or multicast restrictions.
The existing automated board tests use localhost-only DDS; restart the actual
bridge with the cross-machine environment to expose it to host tools.
Unix IPC remains local to the board. RViz subscribes to DDS topics, not to that socket.
The current `/robot_control_ipc/status` is diagnostic JSON in String and has no
standard RViz display. TF/Odometry/LaserScan/PointCloud2 must be provided by future
adapters/sensors before there is robot geometry or sensor data to visualize.

## Native Humble on the board

The identified board is Ubuntu22.04.5 arm64. The operator has installed native
Humble ros-base under `/opt/ros/humble`; imports and the existing IPC/DDS regression
have now passed against that installation. Package versions and archived evidence
are in [the board baseline](../../docs/verification/IPC_BOARD_BASELINE.md).
The earlier private runtime remains historical test evidence.

Open a fresh board terminal to avoid inheriting the private runtime environment:

```bash
# Board's configured shell is zsh; use setup.bash instead if running Bash.
source /opt/ros/humble/setup.zsh
export ROS_DOMAIN_ID=186 ROS_LOCALHOST_ONLY=0 RMW_IMPLEMENTATION=rmw_fastrtps_cpp
# From the checkout or isolated board stage, with the configured local IPC endpoint:
python3 ros2/robot_control_ipc_node.py --ros-args \
  -p socket_path:=/path/to/private-ipc/control.sock
```

Only start the physical control app under its separately reviewed device conditions.
The socket belongs to a board-local app or device-free fixture, not the host.
Do not combine native `/opt/ros/humble` with the old private runtime library paths.
For another Jammy board, use the
[official Humble apt instructions](https://docs.ros.org/en/humble/Installation/Ubuntu-Install-Debs.html).
The installation preparation used official ros2-apt-source1.3.0 and
`apt-get --no-remove install ros-humble-ros-base`, without scheduling a full-system upgrade.
System installation was performed by the operator.


## Restore and enter

From the repository root, in Bash:

```bash
source docker/ros2/image.lock
docker pull --platform "$platform" "$upstream_image"
docker tag "$upstream_image" "$image_name:$image_tag"
```

Start an ephemeral non-root shell with host-owned workspace output:

```bash
source docker/ros2/image.lock
docker run --rm -it --pull=never --platform "$platform" \
  --user "$(id -u):$(id -g)" \
  --env USER="$(id -un)" --env LOGNAME="$(id -un)" \
  --env HOME=/tmp/ros2-home --env ROS_LOCALHOST_ONLY=1 \
  --read-only --tmpfs /tmp:rw,nosuid,nodev \
  --network none --cap-drop ALL --security-opt no-new-privileges \
  --mount "type=bind,source=$PWD,target=/workspace" \
  --workdir /workspace "$upstream_image" \
  bash -c 'mkdir -p "$HOME"; exec bash --norc'
```

The official entrypoint sources Humble. USER/LOGNAME are necessary when using
the host numeric UID, which has no passwd entry in the official image; otherwise
`ros2 pkg create` fails in Python getpass. The default command above has no device
access or external network. Choose a separately scoped networking setup when
later integration requires it. Do not mount physical CAN/UART to this shell.

## Verified contents and provenance — 2026-09-16

| Item | Observed version |
| --- | --- |
| GCC / G++ | Ubuntu 11.4.0-1ubuntu1~22.04.3 |
| CMake | 3.22.1 |
| ros-humble-ros-base | 0.10.0-1jammy.20260804.204550 |
| python3-colcon-core | 0.21.1+upstream-1 |
| python3-colcon-common-extensions | 0.3.0-100 |
| python3-rosdep | 0.26.0-1 |
| python3-vcstool | 0.3.0-1 |

Upstream is the Docker Official Image `library/ros`, generated from
`osrf/docker_images` revision recorded in [image.lock](image.lock), directory
`ros/humble/ubuntu/jammy/ros-base`. The platform digest freezes the installed
layers; the upstream recipe's moving apt indexes are not rerun locally.

This image is a distribution of separately licensed packages, not a single
new library license. ROS package declarations and system-package copyright
files are available under `/opt/ros/humble/share/*/package.xml` and
`/usr/share/doc/*/copyright`. Its test impact is limited to development smoke
checks; no new core CMake link dependency is introduced.

Actual validation used `out/ros2-smoke` as a disposable bind-mounted workspace:

1. Verified image platform/digest and presence of colcon, rosdep, vcs, CMake,
   rclpy and std_msgs.
2. As host UID/GID, with read-only root filesystem and network disabled, ran
   `ros2 pkg create image_smoke --build-type ament_cmake --node-name smoke --dependencies rclcpp --license Apache-2.0`.
3. `colcon build --cmake-args -DBUILD_TESTING=OFF` built one package.
4. After sourcing `install/setup.bash`, `timeout 5 ros2 run image_smoke smoke`
   printed `hello world image_smoke package` and exited 0.

This validates package creation, compilation and executable launching, not DDS
communication or hardware behavior. Do not count the generated smoke package as
application implementation. Rebuilds/install/log files remain under ignored out/.
