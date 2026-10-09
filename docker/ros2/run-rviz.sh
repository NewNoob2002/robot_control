#!/usr/bin/env bash
# Run Humble RViz/ROS CLI on the host desktop, sharing only X11 authentication and the workspace.
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
image='osrf/ros@sha256:fb07245b32187d74350be25323d8ad2f8ca5c25c325759911a1eff2267a49c1e'
[[ -n "${DISPLAY:-}" && -n "${XAUTHORITY:-}" && -f "$XAUTHORITY" ]] || {
  echo 'Run from the host graphical desktop terminal with DISPLAY and XAUTHORITY set.' >&2
  exit 2
}
docker image inspect "$image" >/dev/null 2>&1 || {
  printf 'Desktop image is absent. Run: docker pull --platform linux/amd64 %s\n' "$image" >&2
  exit 2
}
if [[ $# -eq 0 ]]; then set -- rviz2; fi
mkdir -p "$repo_root/out/rviz-config"
# ponytail: software OpenGL avoids GPU-specific mounts; add GPU access if point-cloud rendering is too slow.
exec docker run --rm --init --platform linux/amd64 --network host \
  --user "$(id -u):$(id -g)" --hostname "$(hostname)" \
  --read-only --tmpfs /tmp:rw,nosuid,nodev --cap-drop ALL --security-opt no-new-privileges \
  --env DISPLAY --env XAUTHORITY=/tmp/robot-control-xauth \
  --env HOME --env USER --env LOGNAME \
  --env QT_QPA_PLATFORM=xcb --env QT_X11_NO_MITSHM=1 --env LIBGL_ALWAYS_SOFTWARE=1 \
  --env ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-186}" --env ROS_LOCALHOST_ONLY=0 \
  --env RMW_IMPLEMENTATION=rmw_fastrtps_cpp --env ROS_LOG_DIR=/tmp/ros-logs \
  --mount type=bind,source=/tmp/.X11-unix,target=/tmp/.X11-unix,readonly \
  --mount "type=bind,source=$XAUTHORITY,target=/tmp/robot-control-xauth,readonly" \
  --mount "type=bind,source=$repo_root/out/rviz-config,target=$HOME/.rviz2" \
  --mount "type=bind,source=$repo_root,target=/workspace" \
  --workdir /workspace "$image" "$@"
