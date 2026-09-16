# ROS2 Humble development image

This is a host **amd64 / Ubuntu 22.04 / ROS2 Humble ros-base** environment.
It reuses the official image by immutable platform digest; no custom Dockerfile
or duplicate package installation is needed. It is independent of the core
cross-build runner and does not implement the Phase 13 ROS2 adapter.

The native binaries built here are amd64, not RK3588 aarch64 artifacts.
Use the existing cross runner with the real target sysroot for the core.
Target ROS2 deployment and DDS/robot integration remain separate acceptance gates.
RViz/Gazebo and a desktop image are outside this CLI development environment.

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
