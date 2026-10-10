#!/usr/bin/env bash
# Build and check real Humble DDS in the pinned image, without hardware or external networking.
set -euo pipefail
readonly root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
# shellcheck source=../../docker/ros2/image.lock
source "${root}/docker/ros2/image.lock"

docker run --rm --pull=never --platform "${platform}" \
  --user "$(id -u):$(id -g)" \
  --env USER="$(id -un)" --env LOGNAME="$(id -un)" \
  --env ROS_HOME=/tmp/ros2 --env ROS_LOG_DIR=/tmp/ros2/log \
  --env PYTHONDONTWRITEBYTECODE=1 --env ROS_LOCALHOST_ONLY=1 \
  --env ROS_DOMAIN_ID=187 --env RMW_IMPLEMENTATION=rmw_fastrtps_cpp \
  --read-only --tmpfs /tmp:rw,nosuid,nodev --network none \
  --cap-drop ALL --security-opt no-new-privileges \
  --mount "type=bind,source=${root},target=/workspace" --workdir /workspace \
  "${upstream_image}" bash -euc '
    cmake -S . -B out/build/ros2-ipc-ci -DCMAKE_BUILD_TYPE=Debug \
      -DBUILD_TESTING=ON -DROBOT_CONTROL_BUILD_TEST_APP=ON
    cmake --build out/build/ros2-ipc-ci --parallel 2 --target robot_control_ipc_fixture
    timeout 50 python3 scripts/test/test_ros2_ipc.py out/build/ros2-ipc-ci/app/robot_control_ipc_fixture
  '
