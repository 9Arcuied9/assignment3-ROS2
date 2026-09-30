# RoboMaster assignment3 ROS2


## 仓库结构

```text
assignment3-ROS2/                       # 仓库根，同时作为 colcon 工作空间
├── README.md
├── docs/
│   ├── ROS2Tutorial.md                 # ROS 2 教程
│   └── assignment.md                   # 作业要求
└── src/hikrobot_camera/                # ROS 2 功能包
    ├── package.xml                     # 包信息与依赖
    ├── CMakeLists.txt                  # 构建与安装配置（含 MVS SDK 路径探测）
    ├── include/hikrobot_camera/
    │   └── camera_node.hpp             # 节点类声明
    ├── src/
    │   ├── main.cpp                    # 程序入口
    │   └── camera_node.cpp             # 相机驱动实现（采集/参数/重连）
    ├── launch/camera.launch.py         # 启动文件
    ├── config/camera.yaml              # 参数配置（目前只放 use_sim_time）
    ├── cmake/                          # 占位目录，未使用
    └── test/                           # 占位目录，未使用
```

## 环境要求

- Ubuntu 22.04
- ROS 2 Humble
- 海康机器人 **MVS SDK**（Linux，x86_64）
- OpenCV（系统包 `libopencv-dev`）
- CMake ≥ 3.13

## 依赖安装

### 1. ROS 2 与开发工具

按 [ROS 2 官方文档](https://docs.ros.org/en/humble/Installation/Ubuntu-Install-Debians.html) 安装 Humble，
并确保 `ros2`、`colcon`、`rosdep` 可用：

```bash
sudo apt install ros-humble-desktop python3-colcon-common-extensions python3-rosdep
```

### 2. 海康 MVS SDK

1. 到 [海康机器人下载中心](https://www.hikrobotics.com/cn/machinevision/service/download/?module=0)
   选择 **Linux** 平台、对应架构（x86_64）下载。
2. 按随附文档**执行安装脚本**，默认会装到 `/opt/MVS`。

本包通过 CMake 变量 `MVS_ROOT` 定位 SDK，按以下顺序自动探测：

> SDK 自带的 MVS 客户端（`/opt/MVS/bin/MVS`）可用来快速验证相机，但**启动本节点前必须先关掉** ——
> 相机是独占设备，客户端开着时节点会报 `0x80000203`（设备已被占用）。

### 3. ROS / 系统依赖

```bash
sudo rosdep init 
rosdep update
rosdep install --from-paths src --ignore-src -r -y --rosdistro humble
```

`rosdep` 会依据 `package.xml` 安装 `rclcpp`、`rcl_interfaces`、`image_transport`、`cv_bridge`、
`sensor_msgs`、`std_msgs`、`libopencv-dev` 等。

## 编译

```bash
source /opt/ros/humble/setup.bash      # zsh 用 setup.zsh
colcon build --packages-select hikrobot_camera
source install/setup.bash
```

如果 MVS 不在默认位置，显式指定根目录：

```bash
colcon build --packages-select hikrobot_camera --cmake-args -DMVS_ROOT=/你的/MVS
# 或者
export MVS_ROOT=/你的/MVS && colcon build --packages-select hikrobot_camera
```

## 运行

### 启动节点

```bash
source /opt/ros/humble/setup.bash      # zsh 用 setup.zsh
source install/setup.bash

# 方式一：launch
ros2 launch hikrobot_camera camera.launch.py

# 方式二：直接跑可执行文件
ros2 run hikrobot_camera camera_node
ros2 run hikrobot_camera camera_node --ros-args -p exposure_time:=8000.0 -p gain:=10.0
```

### 确认启动成功

启动日志里应该出现（以默认参数为例）：

```text
[INFO] [hikrobot_camera]: 启动：serial='' topic='image_raw' exposure=16000.0us gain=0.00 fps=30.0 format=''
[INFO] [hikrobot_camera]: 相机已连接：serial=<你的相机序列号>
[INFO] [hikrobot_camera]: 实际帧率 30.0 fps（设定 30.0 fps）
```

另开一个终端检查话题：

```bash
source /opt/ros/humble/setup.bash && source install/setup.bash
ros2 topic list             # 应能看到 /image_raw
ros2 topic hz /image_raw    # 实际发布帧率
```


## 可配置参数

### 启动时设置（launch 参数）

```bash
ros2 launch hikrobot_camera camera.launch.py <参数>:=<值>
ros2 launch hikrobot_camera camera.launch.py --show-args      # 查看可用参数
```

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `serial_number` | `''` | 相机序列号；留空表示使用枚举到的第一台 |
| `image_topic` | `image_raw` | 图像发布话题名（仅启动时生效） |
| `exposure_time` | `16000.0` | 曝光时间，微秒 us |
| `gain` | `0.0` | 增益，dB（会自动关闭自动增益） |
| `frame_rate` | `30.0` | 帧率，Hz |
| `pixel_format` | `''` | 像素格式，留空表示不改动相机当前设置 |

`pixel_format` 可取值：`Mono8`、`Mono16`、`BGR8`、`RGB8`、`BayerRG8`、`BayerGR8`、`BayerGB8`、`BayerBG8`
（还需相机硬件支持）。

示例：

```bash
ros2 launch hikrobot_camera camera.launch.py exposure_time:=8000 gain:=10
ros2 launch hikrobot_camera camera.launch.py frame_rate:=60 pixel_format:=BayerRG8
ros2 launch hikrobot_camera camera.launch.py serial_number:=<你的序列号>
ros2 launch hikrobot_camera camera.launch.py params_file:=/absolute/path/to/my.yaml
```

节点发布的话题是 `/image_raw`（`sensor_msgs/msg/Image`，`frame_id = hik_camera`）。

### 指定要连接的相机（序列号）

只用一台相机时留空即可（取枚举到的第一台）；多台相机或想固定连某台时：

```bash
ros2 launch hikrobot_camera camera.launch.py serial_number:=<你的序列号>
```

### 运行时设置（ROS 参数）

其中 4 个参数支持运行时动态修改：

```bash
ros2 param list /hikrobot_camera
ros2 param get  /hikrobot_camera frame_rate
ros2 param set  /hikrobot_camera exposure_time 5000.0
ros2 param set  /hikrobot_camera frame_rate 60.0
```
- 仅 `exposure_time`、`gain`、`frame_rate`、`pixel_format` 可在运行时修改。



## 查看实际状态

### 1. 参数的当前值

```bash
ros2 param list /hikrobot_camera                 # 列出所有参数
ros2 param get  /hikrobot_camera frame_rate      # 读单个参数
ros2 param get  /hikrobot_camera exposure_time
```



这行日志可以在四个地方看到：

| 位置 | 怎么看 |
| --- | --- |
| 终端 | `ros2 run` 时直接打在终端；`ros2 launch` 时每行前面多一个 `[camera_node-1]` 前缀 |
| 日志文件 | `~/.ros/log/camera_node_<pid>_<时间戳>.log`；实时跟踪 `tail -f ~/.ros/log/camera_node_*.log` |
| `/rosout` 话题 | `ros2 topic echo /rosout --field msg`（所有节点的日志都会转发到这个话题，**带文件名和行号**） |
| rqt_console | `ros2 run rqt_console rqt_console`（图形界面，可按级别 / 节点过滤） |


```bash
ros2 topic hz /image_raw      # 实测话题的发布频率
```

## 查看图像

```bash
rviz2
```


检查话题状态：

```bash
ros2 topic list
ros2 topic hz /image_raw
```


## 已知问题与未完成项

| 项 | 说明 |
| --- | --- |
| 偶发 `Node not found` | 极少数情况下节点仍在出图，但 `ros2 node list` 为空、`ros2 param list <节点>` 报 `Node not found`（DDS 发现异常）。**重启节点即可恢复**，重启 `ros2 daemon` 无效 |
|sdk安装位置随环境改变而不同|从git上克隆后可能需要自己在更改mvs sdk的地址|
