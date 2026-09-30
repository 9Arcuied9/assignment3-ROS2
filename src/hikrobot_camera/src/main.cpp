#include <memory>

#include "hikrobot_camera/camera_node.hpp"
#include "rclcpp/rclcpp.hpp"

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<hikrobot_camera::CameraNode>();
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
