"""Launch one bridge or monitor participant; a DDS profile applies only to that child."""
from pathlib import Path

# ROS dependencies are checked in the pinned Humble image, outside the host analyzer.
from launch import LaunchDescription  # pyright: ignore[reportAttributeAccessIssue]
from launch.actions import DeclareLaunchArgument, OpaqueFunction  # pyright: ignore[reportMissingImports]
from launch.substitutions import LaunchConfiguration  # pyright: ignore[reportMissingImports]
from launch_ros.actions import Node  # pyright: ignore[reportMissingImports]


def start(context):
    """Validate startup-only arguments before starting a read-only Python participant."""
    role = LaunchConfiguration('role').perform(context)
    if role not in ('bridge', 'monitor'):
        raise ValueError('role must be bridge or monitor')
    profile = LaunchConfiguration('dds_profile').perform(context)
    if profile and (not Path(profile).is_absolute() or not Path(profile).is_file()):
        raise ValueError('dds_profile must name an existing absolute file')
    simulated = LaunchConfiguration('use_sim_time').perform(context).lower()
    if simulated not in ('true', 'false'):
        raise ValueError('use_sim_time must be true or false')
    parameters = {'use_sim_time': simulated == 'true'}
    if role == 'bridge':
        path = LaunchConfiguration('socket_path').perform(context)
        if not Path(path).is_absolute():
            raise ValueError('bridge requires an absolute socket_path')
        parameters['socket_path'] = path
    else:
        parameters['status_topic'] = LaunchConfiguration('status_topic').perform(context)
    return [Node(package='robot_control_ros2',
                 executable='robot_control_ipc_node' if role == 'bridge' else 'robot_control_ipc_monitor',
                 namespace=LaunchConfiguration('namespace'), parameters=[parameters], output='screen',
                 additional_env={'FASTRTPS_DEFAULT_PROFILES_FILE': profile} if profile else {})]


def generate_launch_description():
    """Expose explicit socket/topic/namespace/profile arguments without starting hardware."""
    return LaunchDescription([
        DeclareLaunchArgument('role', default_value='bridge', choices=['bridge', 'monitor']),
        DeclareLaunchArgument('socket_path', default_value=''),
        DeclareLaunchArgument('status_topic', default_value='robot_control_ipc/status'),
        DeclareLaunchArgument('namespace', default_value=''),
        DeclareLaunchArgument('dds_profile', default_value=''),
        DeclareLaunchArgument('use_sim_time', default_value='false'),
        OpaqueFunction(function=start),
    ])
