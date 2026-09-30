#ifndef HIKROBOT_CAMERA__CAMERA_NODE_HPP_
#define HIKROBOT_CAMERA__CAMERA_NODE_HPP_

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <image_transport/image_transport.hpp>
#include <rclcpp/rclcpp.hpp>



namespace hikrobot_camera
{

class CameraNode : public rclcpp::Node
{
public:
  explicit CameraNode(const rclcpp::NodeOptions & options = rclcpp::NodeOptions());
  ~CameraNode() override;
private:
  bool connect_camera();
  void disconnect_camera();

  bool set_exposure(double exposure_us);
  bool set_gain(double gain_db);
  bool set_frame_rate(double frame_rate_hz);
  bool set_pixel_format(const std::string & fmt);

  
  void grab_loop();
  rcl_interfaces::msg::SetParametersResult on_param_change(
    const std::vector<rclcpp::Parameter> & params);

  std::string serial_number_;
  std::string image_topic_;
  std::string pixel_format_;
  double exposure_time_{20000.0};
  double gain_{0.0};
  double frame_rate_{30.0};

  void * handle_{nullptr};   
  bool grabbing_{false};     
  std::mutex mutex_;

  std::atomic<bool> run_{false};
  std::thread grab_thread_;

  image_transport::Publisher image_pub_;
  rclcpp::Node::OnSetParametersCallbackHandle::SharedPtr param_callback_;

  int frame_count_{0};
  std::chrono::steady_clock::time_point stat_time_;
  
};

} 

#endif
