#include "hikrobot_camera/camera_node.hpp"
#include <MvCameraControl.h>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>

#include <cv_bridge/cv_bridge.h>
#include <opencv2/imgproc.hpp>
#include <std_msgs/msg/header.hpp>

namespace hikrobot_camera
{
using namespace std::chrono_literals;
namespace
{

std::string device_serial(const MV_CC_DEVICE_INFO & info)
{
  const unsigned char * buf = info.SpecialInfo.stUsb3VInfo.chSerialNumber;
  std::size_t len = 0;
  while (len < sizeof(info.SpecialInfo.stUsb3VInfo.chSerialNumber) && buf[len] != '\0') {
    ++len;
  }
  return std::string(reinterpret_cast<const char *>(buf), len);
}


bool convert_image(const MV_FRAME_OUT & frame, cv::Mat & out, std::string & encoding)
{
  const int w = frame.stFrameInfo.nWidth;
  const int h = frame.stFrameInfo.nHeight;
  unsigned char * data = frame.pBufAddr;

  switch (frame.stFrameInfo.enPixelType) {
    case PixelType_Gvsp_Mono8:
      out = cv::Mat(h, w, CV_8UC1, data);
      encoding = "mono8";
      return true;
    case PixelType_Gvsp_Mono16:
      out = cv::Mat(h, w, CV_16UC1, data);
      encoding = "mono16";
      return true;
    case PixelType_Gvsp_BGR8_Packed:
      out = cv::Mat(h, w, CV_8UC3, data);
      encoding = "bgr8";
      return true;
    case PixelType_Gvsp_RGB8_Packed:
      cv::cvtColor(cv::Mat(h, w, CV_8UC3, data), out, cv::COLOR_RGB2BGR);
      encoding = "bgr8";
      return true;
    case PixelType_Gvsp_BayerRG8:
      cv::cvtColor(cv::Mat(h, w, CV_8UC1, data), out, cv::COLOR_BayerRG2BGR);
      encoding = "bgr8";
      return true;
    case PixelType_Gvsp_BayerGR8:
      cv::cvtColor(cv::Mat(h, w, CV_8UC1, data), out, cv::COLOR_BayerGR2BGR);
      encoding = "bgr8";
      return true;
    case PixelType_Gvsp_BayerGB8:
      cv::cvtColor(cv::Mat(h, w, CV_8UC1, data), out, cv::COLOR_BayerGB2BGR);
      encoding = "bgr8";
      return true;
    case PixelType_Gvsp_BayerBG8:
      cv::cvtColor(cv::Mat(h, w, CV_8UC1, data), out, cv::COLOR_BayerBG2BGR);
      encoding = "bgr8";
      return true;
    default:
      return false;
  }
}

}
CameraNode::CameraNode(const rclcpp::NodeOptions & options)
: Node("hikrobot_camera", options)
{
  serial_number_ = this->declare_parameter<std::string>("serial_number", "");
  image_topic_ = this->declare_parameter<std::string>("image_topic", "image_raw");
  exposure_time_ = this->declare_parameter<double>("exposure_time", 16000.0);
  gain_ = this->declare_parameter<double>("gain", 0.0);
  frame_rate_ = this->declare_parameter<double>("frame_rate", 30.0);
  pixel_format_ = this->declare_parameter<std::string>("pixel_format", "");

  image_pub_ = image_transport::create_publisher(this, image_topic_);
  param_callback_ = this->add_on_set_parameters_callback(
    [this](const std::vector<rclcpp::Parameter> & p) {return on_param_change(p);});

  RCLCPP_INFO(
    this->get_logger(), "启动：serial='%s' topic='%s' exposure=%.1fus gain=%.2f fps=%.1f format='%s'",
    serial_number_.c_str(), image_topic_.c_str(), exposure_time_, gain_, frame_rate_,
    pixel_format_.c_str());

  stat_time_ = std::chrono::steady_clock::now();
  run_ = true;
  grab_thread_ = std::thread(&CameraNode::grab_loop, this);
}

CameraNode::~CameraNode()
{
  run_ = false;
  if (grab_thread_.joinable()) {
    grab_thread_.join();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  disconnect_camera();
}


// ================================================================== 设备管理

bool CameraNode::connect_camera()
{
  MV_CC_DEVICE_INFO_LIST dev_list;
  std::memset(&dev_list, 0, sizeof(dev_list));//清零
  if (MV_CC_EnumDevices(MV_USB_DEVICE, &dev_list) != MV_OK || dev_list.nDeviceNum == 0) {
    RCLCPP_WARN_THROTTLE(
      this->get_logger(), *this->get_clock(), 5000, "未发现相机，等待重连...");
    return false;
  }

  MV_CC_DEVICE_INFO * target = nullptr;
  for (unsigned int i = 0; i < dev_list.nDeviceNum; ++i) {
    if (dev_list.pDeviceInfo[i] == nullptr) {
      continue;
    }
    if (serial_number_.empty() || device_serial(*dev_list.pDeviceInfo[i]) == serial_number_) {
      target = dev_list.pDeviceInfo[i];
      break;
    }
  }
  if (target == nullptr) {
    RCLCPP_ERROR(this->get_logger(), "没有序列号为 '%s' 的相机", serial_number_.c_str());
    return false;
  }

  int ret = MV_CC_CreateHandle(&handle_, target);
  if (ret != MV_OK) {
    RCLCPP_WARN_THROTTLE(
      this->get_logger(), *this->get_clock(), 5000, "创建相机句柄失败 0x%x", ret);
    handle_ = nullptr;
    return false;
  }
  ret = MV_CC_OpenDevice(handle_);
  if (ret != MV_OK) {
    RCLCPP_WARN_THROTTLE(
      this->get_logger(), *this->get_clock(), 5000,
      "打开相机失败 0x%x（可能已被其他程序占用）", ret);
    MV_CC_DestroyHandle(handle_);
    handle_ = nullptr;
    return false;
  }

  // 恢复上一次的配置（首次连接时就是参数默认值）
  if (!set_exposure(exposure_time_)) {
    RCLCPP_WARN(this->get_logger(), "下发曝光 %.1f us 失败", exposure_time_);
  }
  if (!set_gain(gain_)) {
    RCLCPP_WARN(this->get_logger(), "下发增益 %.2f 失败", gain_);
  }
  if (!set_frame_rate(frame_rate_)) {
    RCLCPP_WARN(this->get_logger(), "下发帧率 %.1f fps 失败", frame_rate_);
  }
  if (!pixel_format_.empty() && !set_pixel_format(pixel_format_)) {
    RCLCPP_WARN(this->get_logger(), "下发像素格式 %s 失败", pixel_format_.c_str());
  }

  ret = MV_CC_StartGrabbing(handle_);
  if (ret != MV_OK) {
    RCLCPP_WARN_THROTTLE(
      this->get_logger(), *this->get_clock(), 5000, "开始取流失败 0x%x", ret);
    disconnect_camera();
    return false;
  }
  grabbing_ = true;

  RCLCPP_INFO(this->get_logger(), "相机已连接：serial=%s", device_serial(*target).c_str());
  return true;
}

 

void CameraNode::disconnect_camera()
{
  if (handle_ == nullptr) {
    return;
  }
  if (grabbing_) {
    MV_CC_StopGrabbing(handle_);
    grabbing_ = false;
  }
  MV_CC_CloseDevice(handle_);
  MV_CC_DestroyHandle(handle_);
  handle_ = nullptr;
}

// ================================================================ 相机参数写入

bool CameraNode::set_exposure(double exposure_us)
{
  if (handle_ == nullptr) {
    return false;
  }
  MV_CC_SetEnumValue(handle_, "ExposureAuto", 0);  // 手动曝光前先关自动曝光
  return MV_CC_SetFloatValue(handle_, "ExposureTime", static_cast<float>(exposure_us)) == MV_OK;
}

bool CameraNode::set_gain(double gain_db)
{
  if (handle_ == nullptr) {
    return false;
  }
  MV_CC_SetEnumValue(handle_, "GainAuto", 0);  // 手动增益前先关自动增益
  return MV_CC_SetFloatValue(handle_, "Gain", static_cast<float>(gain_db)) == MV_OK;
}

bool CameraNode::set_frame_rate(double frame_rate_hz)
{
  if (handle_ == nullptr) {
    return false;
  }

  if (MV_CC_SetBoolValue(handle_, "AcquisitionFrameRateEnable", true) != MV_OK) {
    return false;
  }
  return MV_CC_SetFloatValue(
    handle_, "AcquisitionFrameRate", static_cast<float>(frame_rate_hz)) == MV_OK;
}

bool CameraNode::set_pixel_format(const std::string & fmt)
{
  if (handle_ == nullptr) {
    return false;
  }

  if (grabbing_) {
    MV_CC_StopGrabbing(handle_);
  }
  const int ret = MV_CC_SetEnumValueByString(handle_, "PixelFormat", fmt.c_str());
  if (grabbing_) {
    MV_CC_StartGrabbing(handle_);
  }
  return ret == MV_OK;
}

// ============================================================== 动态参数回调

rcl_interfaces::msg::SetParametersResult CameraNode::on_param_change(
const std::vector<rclcpp::Parameter> & params)
{
  rcl_interfaces::msg::SetParametersResult result;
  result.successful = true;
  result.reason = "ok";
  std::lock_guard<std::mutex> lock(mutex_);

  for (const auto & param : params)
  {
    const std::string & name = param.get_name();
    bool ok = true;

    if (name == "serial_number" || name == "image_topic")
    {
      result.successful = false;
      result.reason = "serial_number / image_topic can only set at startup.";
      return result;
    }


    if (name == "exposure_time" || name == "gain" || name == "frame_rate")
    {
      const double v = param.as_double();
      if (!std::isfinite(v))
      {
        result.successful = false;
        result.reason = name + " 必须是有限数值。";
        return result;
      }
      if (name == "exposure_time" && v <= 0.0)
      {
        result.successful = false;
        result.reason = "exposure_time 必须大于 0 us。";
        return result;
      }
      if (name == "frame_rate" && v <= 0.0)
      {
        result.successful = false;
        result.reason = "frame_rate 必须大于 0 Hz。";
        return result;
      }
    }

    if (name == "exposure_time")
    {
      double new_val = param.as_double();   // 临时变量存新值
      ok = set_exposure(new_val);
      if (ok)
      {
        exposure_time_ = new_val;          // 设置硬件成功，才更新缓存
      }
    }
    else if (name == "gain")
    {
      double new_val = param.as_double();
      ok = set_gain(new_val);
      if (ok)
      {
        gain_ = new_val;
      }
    }
    else if (name == "frame_rate")
    {
      double new_val = param.as_double();
      ok = set_frame_rate(new_val);
      if (ok)
      {
        frame_rate_ = new_val;
      }
    }
    else if (name == "pixel_format")
    {
      std::string new_val = param.as_string();
      ok = true;
      if (!new_val.empty())
      {
        ok = set_pixel_format(new_val);
      }
      if (ok)
      {
        pixel_format_ = new_val;
      }
    }

    // 相机在线，并且SDK设置失败 → 拒绝本次参数修改
    if (!ok && handle_ != nullptr)
    {
      result.successful = false;
      result.reason = "SDK 拒绝设置参数 " + name;
      return result;
    }
    // 相机离线（handle_ == nullptr）：
    // 此时 set_xxx 直接返回false，我们不拒绝参数，直接把新值存入缓存
    // 等后续重连connect_camera的时候，会自动下发缓存参数
    else if (!ok && handle_ == nullptr)
    {
      if(name == "exposure_time") exposure_time_ = param.as_double();
      else if(name == "gain") gain_ = param.as_double();
      else if(name == "frame_rate") frame_rate_ = param.as_double();
      else if(name == "pixel_format") pixel_format_ = param.as_string();
    }
  }
  return result;
}


// ================================================================== 采集循环

void CameraNode::grab_loop()
{
  MV_FRAME_OUT frame;
  std::memset(&frame, 0, sizeof(frame));

  while (run_ && rclcpp::ok()) {
    bool retry = false;

    {
      std::lock_guard<std::mutex> lock(mutex_);

      if (handle_ == nullptr && !connect_camera()) {
        retry = true;
      } else {
        std::memset(&frame, 0, sizeof(frame));
        const int ret = MV_CC_GetImageBuffer(handle_, &frame, 1000);

        // MV_E_NODATA(0x80000007) 是"超时，未收到数据"，不是故障：
        // 低帧率、长曝光时 1s 内没有新帧很正常，继续等待即可。
        // 若把它当故障断开重连，重连后又下发同样的参数，会陷入死循环。
        // MV_E_NODATA 是无符号字面量而 SDK 返回 int，故转成无符号再比，避免符号告警。
        if (static_cast<unsigned int>(ret) == MV_E_NODATA) {
          RCLCPP_DEBUG_THROTTLE(
            this->get_logger(), *this->get_clock(), 5000,
            "等待帧数据超时（设定 %.1f fps），继续等待", frame_rate_);
        } else if (ret != MV_OK) {
          RCLCPP_WARN(this->get_logger(), "取帧失败(0x%x)，断开连接准备重连", ret);
          disconnect_camera();
          retry = true;
        } else {
          cv::Mat image;
          std::string encoding;
          if (convert_image(frame, image, encoding)) {
            auto msg = cv_bridge::CvImage(std_msgs::msg::Header(), encoding, image).toImageMsg();
            msg->header.stamp = this->now();
            msg->header.frame_id = "hik_camera";
            image_pub_.publish(*msg);
            ++frame_count_;
          } else {
            RCLCPP_WARN_THROTTLE(
              this->get_logger(), *this->get_clock(), 5000, "暂不支持的像素格式 0x%x，已丢帧",
              static_cast<unsigned int>(frame.stFrameInfo.enPixelType));
          }
          MV_CC_FreeImageBuffer(handle_, &frame);  // 必须释放，否则会耗尽 SDK 缓存
        }
      }
    }  // 先解锁再休眠，避免长时间阻塞参数回调

    if (retry) {
      std::this_thread::sleep_for(1s);
    }

    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - stat_time_).count();
    if (elapsed >= 5.0) {
      RCLCPP_INFO(
        this->get_logger(), "实际帧率 %.1f fps（设定 %.1f fps）",
        frame_count_ / elapsed, frame_rate_);
      frame_count_ = 0;
      stat_time_ = now;
    }
  }
} 
}

