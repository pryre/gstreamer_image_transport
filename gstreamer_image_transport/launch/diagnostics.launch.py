
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, Shutdown
from launch.substitutions import LaunchConfiguration
from launch.conditions import IfCondition
from launch_ros.actions import ComposableNodeContainer, Node
from launch_ros.descriptions import ComposableNode

from ament_index_python.packages import get_package_share_directory
from pathlib import Path

from typing import Union

def get_settable_arg(name:str, description:str, default_value:Union[str,None] = None):
    cfg = LaunchConfiguration(name)
    arg = DeclareLaunchArgument(
        name,
        default_value=default_value,
        description=description
    )
    return (cfg, arg)

def generate_launch_description():
    (cfg_diag, arg_diag) = get_settable_arg(
        'diag',
        'Aggregate diagnostics',
        default_value='true'
    )

    diag_config = Path(get_package_share_directory('gstreamer_image_transport')) / 'config' / 'diagnostics.yaml'

    return LaunchDescription([
        arg_diag,
        Node(
            name='diagnostic_aggregator',
            namespace='',
            package='diagnostic_aggregator',
            executable='aggregator_node',
            condition=IfCondition(cfg_diag),
            on_exit=Shutdown(),
            parameters=[diag_config],
            arguments=['--ros-args', '--log-level', 'warn'],
        ),
    ])
