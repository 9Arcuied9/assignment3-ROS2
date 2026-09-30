cmake_minimum_required(VERSION 3.8)
project(hikrobot_camera)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(image_transport REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(rcl_interfaces REQUIRED)
find_package(OpenCV REQUIRED)

set(MVS_ROOT "" CACHE PATH "海康 MVS SDK 根目录（该目录下应有 include/ 和 lib/）")

if(NOT MVS_ROOT)
  foreach(_cand
      "$ENV{MVS_ROOT}"
      "$ENV{MVS_SDK_PATH}"
      "$ENV{MVCAM_SDK_PATH}"
      "/opt/MVS"
      "$ENV{HOME}/MVS"
      "$ENV{HOME}/opt/MVS"
      "/usr/local/MVS")
    if(_cand AND EXISTS "${_cand}/include/MvCameraControl.h")
      set(MVS_ROOT "${_cand}")
      break()
    endif()
  endforeach()
endif()

if(NOT MVS_ROOT OR NOT EXISTS "${MVS_ROOT}/include/MvCameraControl.h")
  message(FATAL_ERROR
    "找不到 MVS SDK（需要 <根目录>/include/MvCameraControl.h）。\n"
    "请显式指定，例如：\n"
    "  colcon build --packages-select hikrobot_camera --cmake-args -DMVS_ROOT=/你的/MVS/路径\n"
    "或先 export MVS_ROOT=/你的/MVS/路径")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(_mvs_arch "64")
else()
  set(_mvs_arch "32")
endif()

set(MVS_LIB_DIR "")
foreach(_cand
    "${MVS_ROOT}/lib/${_mvs_arch}"
    "${MVS_ROOT}/lib/x86_64"
    "${MVS_ROOT}/lib/aarch64"
    "${MVS_ROOT}/lib/arm64"
    "${MVS_ROOT}/lib")
  if(EXISTS "${_cand}/libMvCameraControl.so")
    set(MVS_LIB_DIR "${_cand}")
    break()
  endif()
endforeach()

if(NOT MVS_LIB_DIR)
  message(FATAL_ERROR "在 ${MVS_ROOT}/lib 下找不到 libMvCameraControl.so，请检查 SDK 是否完整。")
endif()

message(STATUS "MVS SDK: ${MVS_ROOT}  (lib: ${MVS_LIB_DIR})")

add_executable(camera_node
  src/main.cpp
  src/camera_node.cpp
)
target_compile_features(camera_node PRIVATE cxx_std_17)

target_include_directories(camera_node PRIVATE
  include
  "${MVS_ROOT}/include"
)
target_link_directories(camera_node PRIVATE "${MVS_LIB_DIR}")

ament_target_dependencies(camera_node
  rclcpp
  rcl_interfaces
  image_transport
  cv_bridge
  sensor_msgs
  std_msgs
  OpenCV
)

target_link_libraries(camera_node
  MvCameraControl
)

set_target_properties(camera_node PROPERTIES
  BUILD_RPATH "${MVS_LIB_DIR}"
  INSTALL_RPATH "${MVS_LIB_DIR}"
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(camera_node PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS camera_node DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY launch config DESTINATION share/${PROJECT_NAME})

ament_package()

