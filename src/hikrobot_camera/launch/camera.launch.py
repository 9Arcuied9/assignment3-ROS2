"""启动海康相机节点，并把基本参数暴露成 launch 参数。

用法::

    ros2 launch hikrobot_camera camera.launch.py
    ros2 launch hikrobot_camera camera.launch.py exposure_time:=8000 gain:=10
    ros2 launch hikrobot_camera camera.launch.py frame_rate:=60 pixel_format:=BayerRG8
    ros2 launch hikrobot_camera camera.launch.py serial_number:=DA0823281
    ros2 launch hikrobot_camera camera.launch.py params_file:=/绝对路径/my.yaml
    ros2 launch hikrobot_camera camera.launch.py --show-args      # 查看全部可用参数

参数优先级：launch 参数 > camera.yaml 里的同名项 > 节点内置默认值。
"""

from pathlib import Path
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue

PIXEL_FORMATS = 'Mono8 / Mono16 / BGR8 / RGB8 / BayerRG8 / BayerGR8 / BayerGB8 / BayerBG8'


def generate_launch_description():
    default_params = str(
        Path(get_package_share_directory('hikrobot_camera')) / 'config' / 'camera.yaml'
    )

    declared_arguments = [
        DeclareLaunchArgument(
            'params_file',
            default_value=default_params,
            description='ROS 参数 YAML 文件路径。',
        ),
        DeclareLaunchArgument(
            'serial_number',
            default_value='',
            description='要连接的相机序列号；留空表示使用枚举到的第一台相机。',
        ),
        DeclareLaunchArgument(
            'image_topic',
            default_value='image_raw',
            description='图像发布话题名。',
        ),
        DeclareLaunchArgument(
            'exposure_time',
            default_value='16000.0',
            description='曝光时间，单位微秒 us。',
        ),
        DeclareLaunchArgument(
            'gain',
            default_value='0.0',
            description='增益，单位 dB。',
        ),
        DeclareLaunchArgument(
            'frame_rate',
            default_value='30.0',
            description='帧率，单位 Hz。',
        ),
        DeclareLaunchArgument(
            'pixel_format',
            default_value='',
            description=f'像素格式，留空表示不改动相机当前设置。可选：{PIXEL_FORMATS}',
        ),
    ]

    basic_params = {
        'serial_number': ParameterValue(
            LaunchConfiguration('serial_number'), value_type=str),
        'image_topic': ParameterValue(
            LaunchConfiguration('image_topic'), value_type=str),
        'exposure_time': ParameterValue(
            LaunchConfiguration('exposure_time'), value_type=float),
        'gain': ParameterValue(LaunchConfiguration('gain'), value_type=float),
        'frame_rate': ParameterValue(LaunchConfiguration('frame_rate'), value_type=float),
        'pixel_format': ParameterValue(
            LaunchConfiguration('pixel_format'), value_type=str),
    }

    camera_node = Node(
        package='hikrobot_camera',
        executable='camera_node',
        name='hikrobot_camera',
        output='screen',
        emulate_tty=True,
        parameters=[
            LaunchConfiguration('params_file'),
            basic_params,
        ],
    )

    return LaunchDescription(declared_arguments + [camera_node])
