cmake_minimum_required(VERSION 3.8)
project(hikrobot_camera)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(image_transport REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(OpenCV REQUIRED)

add_executable(camera_node
  src/main.cpp
  src/camera_node.cpp
)
target_compile_features(camera_node PRIVATE cxx_std_17)

# 把MVS头、库目录绑定到目标上，不受顺序影响
target_include_directories(camera_node PRIVATE
  include
  /opt/MVS/include
)
target_link_directories(camera_node PRIVATE /opt/MVS/lib/64)

ament_target_dependencies(camera_node
  rclcpp
  image_transport
  cv_bridge
  sensor_msgs
  std_msgs
  OpenCV
)

# MVS SDK 只提供 libMvCameraControl.so（无 libMVSDK.so）
target_link_libraries(camera_node
  MvCameraControl
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(camera_node PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS camera_node DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY launch config DESTINATION share/${PROJECT_NAME})

ament_package()

