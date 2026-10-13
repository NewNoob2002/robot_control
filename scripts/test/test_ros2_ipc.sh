#!/usr/bin/env bash
# Build and check real Humble DDS in the pinned image, without hardware or external networking.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly root
readonly lock_file="${root}/docker/ros2/image.lock"
platform=""
upstream_image=""
# shellcheck disable=SC1090
source "${lock_file}"
[[ -n "${platform}" && -n "${upstream_image}" ]] || {
  echo "Invalid ROS2 image lock: platform or upstream image is missing" >&2
  exit 3
}

docker run --rm --pull=never --platform "${platform}" \
  --user "$(id -u):$(id -g)" \
  --env USER="$(id -un)" --env LOGNAME="$(id -un)" \
  --env ROS_HOME=/tmp/ros2 --env ROS_LOG_DIR=/tmp/ros2/log \
  --env PYTHONDONTWRITEBYTECODE=1 --env ROS_LOCALHOST_ONLY=1 \
  --env ROS_DOMAIN_ID=187 --env RMW_IMPLEMENTATION=rmw_fastrtps_cpp \
  --read-only --tmpfs /tmp:rw,nosuid,nodev,exec --network none \
  --cap-drop ALL --security-opt no-new-privileges \
  --mount "type=bind,source=${root},target=/workspace" --workdir /workspace \
  "${upstream_image}" bash -euc '
    cmake -S . -B out/build/ros2-ipc-ci -DCMAKE_BUILD_TYPE=Debug \
      -DBUILD_TESTING=ON -DROBOT_CONTROL_BUILD_TEST_APP=ON
    cmake --build out/build/ros2-ipc-ci --parallel 2 --target robot_control_ipc_fixture
    ctest --test-dir out/build/ros2-ipc-ci --output-on-failure -R "^ros2_receipt_contract$"
    timeout 50 python3 scripts/test/test_ros2_ipc.py out/build/ros2-ipc-ci/app/robot_control_ipc_fixture
    cmake --install out/build/ros2-ipc-ci --prefix /tmp/ros2-cmake --component ros2
    AMENT_PREFIX_PATH="/tmp/ros2-cmake:${AMENT_PREFIX_PATH}" timeout 50 python3 \
      scripts/test/test_ros2_package.py out/build/ros2-ipc-ci/app/robot_control_ipc_fixture run
    colcon --log-base /tmp/colcon-log build --base-paths ros2 \
      --build-base /tmp/colcon-build --install-base /tmp/colcon-install --executor sequential
    # Colcon-generated setup references optional variables and is not nounset-compatible.
    set +u
    source /tmp/colcon-install/local_setup.bash
    set -u
    timeout 50 python3 scripts/test/test_ros2_package.py out/build/ros2-ipc-ci/app/robot_control_ipc_fixture launch
  '
