/* README:

    This is an example of using single plin sdicameras as input source, a single Yolov5 NPU for inference,
    and 1 HDMI display.

    Pipeline拓扑结构:
                                     +---+     +---------+    +-----------------+  Q
                                     |   |     |         |    |                 |
                                     | q |     |         |    |                 |
                                     | u |     |   NPU   |  Q |    MsgRouter    |                  +---------------------+
    [SDI Camera 0] -- Msg(id=0) -->  | e | --> |         | -->|  (with routing  | -- Msg(id=0) --> | RGB565 HDMI DISPLAY |
                                     | u |     |  Actor  |    |      table)     |                  +---------------------+
                                     | e |     |         |    |                 |
                                     |   |     |         |    |                 |
                                     +---+     +---------+    +-----------------+

    说明:
    1. 单路 PLin-SDICamera 通过 SDICameraInputActor 采集图像后，发送消息到同一个 message queue.
    2. NPU Actor 读取唯一的 message queue ，不区分source_id处理AI前向。
    3. NPU Actor 处理后的消息通过 MessageRouter，根据 source_id 路由到对应的 HDMI Display Actor。
    4. 每个摄像头和每个显示接收器都有唯一的 source_id (0)，确保消息正确路由。
    5. 这种设计允许系统灵活扩展，添加更多摄像头或处理节点，只需更新路由表。


    准备：
    1. 下载对应位流：https://download.fdwxhb.com/data/04_FMSH-AI/100AI/02_Icraft/v3.31/%e5%8f%82%e8%80%83%e5%ae%9e%e7%8e%b0/%e6%82%9f%e7%a9%ba%e5%bc%80%e5%8f%91%e6%9d%bf/%e5%8d%95%e8%b7%afPLIN+pHDMI%e6%98%be%e7%a4%ba%e5%8f%82%e8%80%83%e5%ae%9e%e7%8e%b0/25060601/
    2. 将 BOOT.bin 放到 /root/bits 下，重启板子
    3. 确保两个网络摄像头开启，配置正确的编码格式和 URL 地址，在 examples/1_single_input+ai/PLin+SingleNet+HDMI/configs 目录下的文件中配置。
*/

// application includes
#include "ai_example/postprocesses.hpp"
#include "ai_example/yolov5_npu_actor.hpp"
#include "aim_follow_controller.hpp"

// pipeline includes
#include "pipeline/actor/message_router.hpp"
#include "pipeline/actor/sdicamera_input_actor.hpp"
#include "pipeline/actor/hdmi_display_actor.hpp"
#include "pipeline/io/input/sdicamera.hpp"
#include "pipeline/memory/buffer_manager.hpp"
// modelzoo_utils includes
#include "file_utils.hpp"
#include "et_device.hpp"
#include "fps_calculator.hpp"
#include "vis_helper.hpp"
#include "icraft_utils.hpp"
// icraft includes
#include <icraft-xrt/core/session.h>
#include "compile_fpai_target.hpp"
// 3rd-party dependency includes
#include <opencv2/opencv.hpp>
#include "yaml-cpp/yaml.h"
// system libs
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <functional>
#include <filesystem>
//加入引用
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <chrono>


using namespace icraft::xrt;
using namespace icraft::xir;
using namespace std::chrono;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

using namespace fpai;

const std::string DEMO_NAME = "sdicamera+yolov5+hdmi";

const size_t BUFFER_COUNT = 4;
const size_t CAMERA_COUNT = 1;



//--------------------------------------------------------将控制参数抽象成常量-----------------------------------------------------------------------
const int BOX_THICKNESS = 4;   // YOLO框线宽，可调

// 单目距离显示参数。
// 公式参考：距离 = 目标真实宽度 * 相机像素焦距 / 检测框像素宽度。
// 实车/实地标定步骤：
// 1. 测量目标真实宽度，单位为米，然后修改 DISTANCE_TARGET_REAL_WIDTH_M。
// 2. 把目标放在相机前方的已知距离处，例如 0.5m、1m、1.5m。
// 3. 实测时读取画面上的检测框像素宽度，或临时打印 box_list[i].width。
// 4. 计算像素焦距：focal_px = box_width_px * known_distance_m / real_width_m。
// 5. 多测几组距离，取平均值后填入下面的 DISTANCE_CAMERA_FOCAL_PX。
const bool DISTANCE_DISPLAY_ENABLE = true;
const float DISTANCE_TARGET_REAL_WIDTH_M = 0.24f; // 实测自行车图片宽度，单位米。
const float DISTANCE_CAMERA_FOCAL_PX = 553.0f;    // 实测平均值：0.5m、1m、1.5m 三组标定后取约 553px。
const float DISTANCE_MIN_BOX_WIDTH_PX = 1.0f;
const float DISTANCE_FILTER_ALPHA = 0.30f;         // 距离低通滤波系数，越小越稳，越大响应越快。

// HDMI 实测辅助：检测到 bicycle 时保存已经画好检测框和距离文字的画面。
// 保存路径在板子运行目录下的 debug_output/，用于核对 HDMI 画面中是否带有距离信息。
const bool DEBUG_SAVE_FRAME_ENABLE = true;
const int DEBUG_SAVE_FRAME_INTERVAL = 1;
const bool DEBUG_SAVE_DETECTION_ONLY = false;
const int DEBUG_SAVE_MAX_FRAMES = 20;
const std::string DEBUG_SAVE_FRAME_DIR = "debug_output";
const std::string DEBUG_SAVE_FRAME_PATH = DEBUG_SAVE_FRAME_DIR + "/latest_frame.jpg";

// 实测诊断开关：不参与控制，只用于确认 YOLO 是否输出了非 bicycle 类别。
// 若 bicycle 没有被识别出来，会用黄色细框显示原始检测结果，便于判断是“完全没检测”还是“类别被识别错”。
const bool DEBUG_DRAW_RAW_DETECTIONS_WHEN_NO_BICYCLE = true;
const bool DEBUG_PRINT_RAW_DETECTION_SUMMARY = true;


// 颜色：OpenCV里是 BGR
const cv::Scalar RETICLE_COLOR = cv::Scalar(0, 0, 0);   // 黑色




//--------------------------------------------------------云台控制参数-----------------------------------------------------------------------
const int DEADZONE_INNER = 1;   // 微小抖动允许


const int CENTER_YAW = 150;
const int CENTER_PITCH = 150;
const int CMD_YAW_MIN = 100;
const int CMD_YAW_MAX = 200;
const int CMD_PITCH_MIN = 100;
const int CMD_PITCH_MAX = 200;
const int MOTOR_RPM_MIN = -1000;
const int MOTOR_RPM_MAX = 1000;
const uint32_t CHASSIS_CAN_ID = 0x201;
const uint32_t GIMBAL_CAN_ID = 0x38A;
const int CAN_BITRATE = 250000;
const uint8_t TRIGGER_STOP = 0x00;
const uint8_t CAN_CONTROL_ENABLE = 0x01;
const bool AIM_FOLLOW_CONTROL_ENABLE = true;
const char *AIM_FOLLOW_CAN_DRYRUN_ENV = "AIM_FOLLOW_CAN_DRYRUN";
const char *AIM_FOLLOW_SYNTHETIC_TARGET_ENV = "AIM_FOLLOW_SYNTHETIC_TARGET";
const float AIM_FOLLOW_TARGET_DISTANCE_M = 1.0f;
const float AIM_FOLLOW_YAW_KP = 38.0f;
const float AIM_FOLLOW_YAW_KD = 8.0f;
const float AIM_FOLLOW_PITCH_KP = 42.0f;
const float AIM_FOLLOW_PITCH_KD = 8.0f;
const bool AIM_FOLLOW_INVERT_YAW = false;
const bool AIM_FOLLOW_INVERT_PITCH = false;
const float AIM_FOLLOW_AIM_DEADZONE_NORM = 0.035f;
const float AIM_FOLLOW_MAX_CMD_STEP = 8.0f;
const float AIM_FOLLOW_DISTANCE_DEADBAND_M = 0.12f;
const float AIM_FOLLOW_FOLLOW_KP_RPM_PER_M = 180.0f;
const int AIM_FOLLOW_MIN_FOLLOW_RPM = 35;
const int AIM_FOLLOW_MAX_FOLLOW_RPM = 160;
const int AIM_FOLLOW_MOTOR1_FORWARD_SIGN = 1;
const int AIM_FOLLOW_MOTOR2_FORWARD_SIGN = 1;
const int AIM_FOLLOW_LOST_HOLD_FRAMES = 5;
const float AIM_FOLLOW_SELECTOR_MAX_CENTER_JUMP_NORM = 0.25f;
const float AIM_FOLLOW_SELECTOR_AREA_SWITCH_RATIO = 1.8f;
const int AIM_FOLLOW_SELECTOR_MAX_LOST_FRAMES = 5;
//--------------------------------------------------------云台控制参数-----------------------------------------------------------------------
static int last_yaw   = CENTER_YAW;
static int last_pitch = CENTER_PITCH;

float estimate_distance_m(float box_width_px) {
    // 距离估计算法：使用单目相机几何原理计算目标距离
    // 公式：距离 = (目标真实宽度 * 相机像素焦距) / 检测框像素宽度
    // 参数：
    //   - box_width_px: 检测框的像素宽度
    // 返回值：
    //   - 距离（米），如果计算无效则返回 -1.0f
    if (!DISTANCE_DISPLAY_ENABLE) {
        return -1.0f;
    }

    aim_follow::DistanceEstimatorConfig cfg;
    cfg.target_real_width_m = DISTANCE_TARGET_REAL_WIDTH_M;
    cfg.focal_length_px = DISTANCE_CAMERA_FOCAL_PX;
    cfg.min_box_width_px = DISTANCE_MIN_BOX_WIDTH_PX;
    cfg.filter_alpha = 1.0f;
    aim_follow::MonocularDistanceEstimator estimator(cfg);
    const auto estimate = estimator.update(box_width_px);
    return estimate.valid ? estimate.raw_distance_m : -1.0f;
}

float filter_distance_m(float current_distance_m) {
    static aim_follow::MonocularDistanceEstimator estimator([] {
        aim_follow::DistanceEstimatorConfig cfg;
        cfg.target_real_width_m = DISTANCE_TARGET_REAL_WIDTH_M;
        cfg.focal_length_px = DISTANCE_CAMERA_FOCAL_PX;
        cfg.min_box_width_px = DISTANCE_MIN_BOX_WIDTH_PX;
        cfg.filter_alpha = DISTANCE_FILTER_ALPHA;
        return cfg;
    }());

    if (!DISTANCE_DISPLAY_ENABLE) {
        return -1.0f;
    }

    if (current_distance_m <= 0.0f || !std::isfinite(current_distance_m)) {
        const auto estimate = estimator.update(0.0f);
        return estimate.filtered_distance_m;
    }

    const float box_width_px = DISTANCE_TARGET_REAL_WIDTH_M * DISTANCE_CAMERA_FOCAL_PX / current_distance_m;
    const auto estimate = estimator.update(box_width_px);
    return estimate.filtered_distance_m;
}



//-------------------------------------------------------加入CAN函数----------------------------------------------------------------
int can_socket = -1;

bool is_env_enabled(const char *name) {
    const char *value = std::getenv(name);
    if (value == nullptr) {
        return false;
    }
    return std::strcmp(value, "1") == 0 ||
           std::strcmp(value, "true") == 0 ||
           std::strcmp(value, "TRUE") == 0 ||
           std::strcmp(value, "yes") == 0 ||
           std::strcmp(value, "YES") == 0;
}

bool is_can_dry_run_enabled() {
    return is_env_enabled(AIM_FOLLOW_CAN_DRYRUN_ENV);
}

bool is_synthetic_target_enabled() {
    return is_env_enabled(AIM_FOLLOW_SYNTHETIC_TARGET_ENV);
}

void configure_can0() {
    // 实测小车 CAN 总线速率为 250kbps。程序启动时主动配置，避免沿用系统上一次的 500kbps 设置。
    const std::string cmd =
        "ip link set can0 down 2>/dev/null || true; "
        "ip link set can0 type can bitrate " + std::to_string(CAN_BITRATE) + " restart-ms 100; "
        "ip link set can0 up";
    const int ret = std::system(cmd.c_str());
    if (ret != 0) {
        std::cerr << "CAN configure can0 failed, command ret=" << ret << std::endl;
    }
}

void can_init() {
    if (is_can_dry_run_enabled()) {
        std::cout << "[CAN DRYRUN] AIM_FOLLOW_CAN_DRYRUN=1, skip can0 configure/open/write."
                  << std::endl;
        return;
    }

    struct sockaddr_can addr;
    struct ifreq ifr;

    configure_can0();

    can_socket = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (can_socket < 0) {
        perror("socket");
        return;
    }

    std::strcpy(ifr.ifr_name, "can0");
    if (ioctl(can_socket, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl");
        return;
    }

    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(can_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return;
    }

    std::cout << "CAN init ok" << std::endl;
}

uint8_t clamp_percent_byte(int value) {
    return static_cast<uint8_t>(std::clamp(value, 0, 200));
}

int16_t clamp_motor_rpm(int rpm) {
    return static_cast<int16_t>(std::clamp(rpm, MOTOR_RPM_MIN, MOTOR_RPM_MAX));
}

bool send_can_frame(const struct can_frame &frame, const std::string &tag) {
    if (is_can_dry_run_enabled()) {
        std::cout << tag << " DRYRUN id=0x" << std::hex << frame.can_id
                  << std::dec << " dlc=" << static_cast<int>(frame.can_dlc)
                  << std::endl;
        return true;
    }

    if (can_socket < 0) {
        std::cerr << tag << " CAN send skipped: socket not initialized" << std::endl;
        return false;
    }

    ssize_t nbytes = write(can_socket, &frame, sizeof(frame));
    if (nbytes != sizeof(frame)) {
        std::cerr << tag << " CAN send failed, nbytes=" << nbytes
                  << ", errno=" << errno << " (" << std::strerror(errno) << ")"
                  << std::endl;
        return false;
    }

    return true;
}



void send_chassis_can_mode(int motor1_rpm,
                           int motor2_rpm,
                           int pitch,
                           int yaw,
                           uint8_t trigger = TRIGGER_STOP,
                           uint8_t enable = CAN_CONTROL_ENABLE) {
    const int16_t motor1 = clamp_motor_rpm(motor1_rpm);
    const int16_t motor2 = clamp_motor_rpm(motor2_rpm);

    struct can_frame frame {};
    frame.can_id = CHASSIS_CAN_ID;
    frame.can_dlc = 8;

    frame.data[0] = static_cast<uint8_t>((motor1 >> 8) & 0xFF);
    frame.data[1] = static_cast<uint8_t>(motor1 & 0xFF);
    frame.data[2] = static_cast<uint8_t>((motor2 >> 8) & 0xFF);
    frame.data[3] = static_cast<uint8_t>(motor2 & 0xFF);
    frame.data[4] = clamp_percent_byte(pitch);
    frame.data[5] = clamp_percent_byte(yaw);
    frame.data[6] = trigger;
    frame.data[7] = enable;

    std::cout << "[CAN CHASSIS] motor1=" << motor1
              << " motor2=" << motor2
              << " pitch=" << pitch
              << " yaw=" << yaw
              << " enable=" << static_cast<int>(enable)
              << std::endl;
    send_can_frame(frame, "[CAN CHASSIS]");
}

void send_gimbal_can_mode(int pitch,
                          int yaw,
                          uint8_t trigger = TRIGGER_STOP) {
    struct can_frame frame {};
    frame.can_id = GIMBAL_CAN_ID;
    frame.can_dlc = 8;

    // 按《武器战1.xlsx》“云台-CAN”表：0x38A = AA pitch bear percu 00 00 00 55。
    frame.data[0] = 0xAA;
    frame.data[1] = clamp_percent_byte(pitch);
    frame.data[2] = clamp_percent_byte(yaw);
    frame.data[3] = trigger;
    frame.data[4] = 0x00;
    frame.data[5] = 0x00;
    frame.data[6] = 0x00;
    frame.data[7] = 0x55;

    std::cout << "[CAN GIMBAL] pitch=" << pitch
              << " yaw=" << yaw
              << " trigger=0x" << std::hex << static_cast<int>(trigger)
              << std::dec << std::endl;
    send_can_frame(frame, "[CAN GIMBAL]");
}






//-------------------------------------------------------加入CAN函数----------------------------------------------------------------






int main(int argc, char *argv[])
{

    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <config yaml>" << std::endl;
        return 1;
    }



    can_init();//can初始化

    std::cout << "[AIM FOLLOW CONFIG] startup enable=" << AIM_FOLLOW_CONTROL_ENABLE
              << " target_distance_m=" << AIM_FOLLOW_TARGET_DISTANCE_M
              << " yaw_kp=" << AIM_FOLLOW_YAW_KP
              << " yaw_kd=" << AIM_FOLLOW_YAW_KD
              << " pitch_kp=" << AIM_FOLLOW_PITCH_KP
              << " pitch_kd=" << AIM_FOLLOW_PITCH_KD
              << " distance_deadband_m=" << AIM_FOLLOW_DISTANCE_DEADBAND_M
              << " follow_kp_rpm_per_m=" << AIM_FOLLOW_FOLLOW_KP_RPM_PER_M
              << " max_follow_rpm=" << AIM_FOLLOW_MAX_FOLLOW_RPM
              << " motor_signs=" << AIM_FOLLOW_MOTOR1_FORWARD_SIGN
              << "," << AIM_FOLLOW_MOTOR2_FORWARD_SIGN
              << std::endl;



    // ----------------- Load system configuration from YAML file -----------------
    YAML::Node config;
    try
    {
        config = YAML::LoadFile(argv[1]);
    }
    catch (const YAML::BadFile &e)
    {
        spdlog::error("Failed to load config file {}: {}", argv[1], e.what());
        return -1;
    }

    // ----------------- 设置 spdlog 日志级别（默认级别为 info） -----------------
    auto log_level = spdlog::level::debug;
    spdlog::set_level(log_level);
    spdlog::info("Log level set to '{}'", spdlog::level::to_string_view(log_level));
    // --- 日志级别设置完毕 ---

    // ---------------------------- 加载各个Pipeline的配置 -----------------------------------
    const auto &sys_yaml = config["sys"];
    const auto &icore_yaml = config["pipeline"]["icore"];
    const auto &display_yaml = config["pipeline"]["display"];
    const auto &camera_yaml = config["pipeline"]["camera"];
    // Parse system configuration
    FPAIConfig fpai_cfg;
    fpai_cfg.device_url = sys_yaml["device"].as<std::string>();
    fpai_cfg.speed_mode = sys_yaml["speedMode"] ? sys_yaml["speedMode"].as<bool>() : false;
    fpai_cfg.compress_ftmp = sys_yaml["compressFtmp"] ? sys_yaml["compressFtmp"].as<bool>() : false;
    fpai_cfg.mmu_mode = sys_yaml["mmuMode"] ? sys_yaml["mmuMode"].as<bool>() : true;
    fpai_cfg.ocm_option = sys_yaml["ocm_option"] ? sys_yaml["ocm_option"].as<int>() : 4;
    fpai_cfg.run_backend = sys_yaml["run_backend"] ? sys_yaml["run_backend"].as<std::string>() : "buyi";
    fpai_cfg.enable_profile = sys_yaml["profile"] ? sys_yaml["profile"].as<bool>() : false;

    // Parse PLin configuration
    const int CAMERA_W = camera_yaml["width"].as<int>();
    const int CAMERA_H = camera_yaml["height"].as<int>();
    const int CAMERA_FPS = camera_yaml["fps"].as<int>();
    const bool VTC_ON = camera_yaml["vtc"] ? camera_yaml["vtc"].as<bool>() : false;

    // Parse encoder configuration
    const int FRAME_W = display_yaml["width"].as<int>();
    const int FRAME_H = display_yaml["height"].as<int>(); // PLin SDICamera推荐使用UYVY

    // Parse icore configuration
    Yolov5Config yolov5_cfg;
    if (icore_yaml["conf"])
        yolov5_cfg.CONF = icore_yaml["conf"].as<float>();
    if (icore_yaml["iou_thresh"])
        yolov5_cfg.IOU_THRESHOLD = icore_yaml["iou_thresh"].as<float>();
    if (icore_yaml["names"])
        yolov5_cfg.NAMES_PATH = icore_yaml["names"].as<std::string>();
    if (fs::exists(yolov5_cfg.NAMES_PATH))
    {
        yolov5_cfg.LABELS = toVector(yolov5_cfg.NAMES_PATH);
        yolov5_cfg.N_CLASS = yolov5_cfg.LABELS.size();
        spdlog::info("Loaded {} class names from {}", yolov5_cfg.N_CLASS, yolov5_cfg.NAMES_PATH);
    }
    else
    {
        spdlog::warn("Names file {} does not exist. No class names loaded.", yolov5_cfg.NAMES_PATH);
    }
    if (icore_yaml["number_of_class"])
        yolov5_cfg.N_CLASS = icore_yaml["number_of_class"].as<int>();
    if (icore_yaml["fpga_nms"])
        yolov5_cfg.FPGA_NMS = icore_yaml["fpga_nms"].as<bool>();
    if (icore_yaml["detpost"])
        yolov5_cfg.DETPOST = icore_yaml["detpost"].as<bool>();
    if (icore_yaml["net_w"])
        yolov5_cfg.NET_W = icore_yaml["net_w"].as<int>();
    if (icore_yaml["net_h"])
        yolov5_cfg.NET_H = icore_yaml["net_h"].as<int>();
    yolov5_cfg.FRAME_W = FRAME_W;
    yolov5_cfg.FRAME_H = FRAME_H;
    if (icore_yaml["jsons"])
        read_node_urls(icore_yaml, "jsons", yolov5_cfg.JSON_PATHS);
    if (icore_yaml["raws"])
        read_node_urls(icore_yaml, "raws", yolov5_cfg.RAW_PATHS);
    if (icore_yaml["anchors"])
        yolov5_cfg.ANCHORS = icore_yaml["anchors"].as<std::vector<std::vector<std::vector<float>>>>();

    std::tuple<int, int, int, int> ratio_bias;
    int RATIO_W = CAMERA_W / yolov5_cfg.NET_W;
    int RATIO_H = CAMERA_H / yolov5_cfg.NET_H;
    int IMG_W = RATIO_W * yolov5_cfg.NET_W;
    int IMG_H = RATIO_H * yolov5_cfg.NET_H;
    int BIAS_W = (FRAME_W - IMG_W) / 2;
    int BIAS_H = (FRAME_H - IMG_H) / 2;
    ratio_bias = std::make_tuple(RATIO_W, BIAS_W, RATIO_H, BIAS_H);

    // ---------------------------- 正式开始配置pipeline和加载网络 -----------------------------------
    // 1. FPAI设备初始化
    auto device = Device::Open(fpai_cfg.device_url.c_str());
    auto fpai_device = device.cast<FPAIDevice>();
#if defined(USE_BUYI_BACKEND)
    fpai_device.mmuModeSwitch(fpai_cfg.mmu_mode); // 关闭MMU
#endif
    // 多个摄像头输入，多个寄存器组
    std::vector<uint64_t> sdicamera_base_addr_group = {
        0x40080000,
        0x40080400,
        0x40080800,
        0x40080C00};
    std::vector<uint64_t> image_make_base_addr_group = {
        0x80000400,
        0x80040000,
        0x80040400,
        0x80040800};

    // 2. 构建 ICORE Actor 的输入、输出队列

    ThreadSafeQueue<InputMessageForIcore> icore_input_queue(BUFFER_COUNT); // WebcamDecoders -> Icore
    ThreadSafeQueue<IcoreMessageForPost> icore_output_queue(BUFFER_COUNT); // Icore -> HDMI Display

    // 3. 使用 std::vector 存储 Actors 和相关对象
    // XXXActor 这类包含线程、锁或引用的对象是不可移动、不可拷贝的。
    // 不能将它们的对象本身直接存储在 std::vector 中, 必须将它们也改为存储 std::unique_ptr
    std::unique_ptr<SDICameraInputActor<FPAIDevice, FPAIBackend>> camera_actor;

    BufferManager buffer_manager(BUFFER_COUNT);

    // 6. 配置NPU Actor
    int icore_id = 0;
    MultiYolov5IcoreActor<FPAIDevice, FPAIBackend> icore_actor(icore_id,
                                                               fpai_device,
                                                               buffer_manager,
                                                               CAMERA_COUNT,
                                                               yolov5_cfg,
                                                               fpai_cfg);
    icore_actor.bindInputQueue(&icore_input_queue);
    icore_actor.bindOutputQueue(&icore_output_queue);
    // ----------------- 为每个流创建独立的后处理状态 -----------------
    // Define the YOLOv5 post-processing function as a lambda.
    std::vector<YoloPostResult> last_results_group(CAMERA_COUNT); // Store last results for each camera
    auto netinfos = icore_actor.getNetInfos();
    std::vector<FPSCalculator> fps_calculators(CAMERA_COUNT); // 为每个流创建一个FPS计算器

    // 7. 配置单路流水线的输入和输出
    int i = 0;
    // 配置摄像头输入，为每个编码器创建独立的配置副本
    // 配置摄像头
    int camera_id = i;
    auto camera = std::make_unique<GenericSDICamera>(camera_id, fpai_device, CAMERA_W, CAMERA_H, camera_fmt::RGB565,
                                                     FRAME_W, FRAME_H, camera_fmt::RGB565, crop_position::center, // RGB565格式用于hdmi显示
                                                     yolov5_cfg.NET_W, yolov5_cfg.NET_H, crop_position::center,   // PL端输入必须经过hardResizePL
                                                     sdicamera_base_addr_group[i], VTC_ON);
#if defined(USE_BUYI_BACKEND)
    camera_actor = std::make_unique<SDICameraInputActor<FPAIDevice, FPAIBackend>>(
        camera->getSourceId(),
        std::move(camera),
        fpai_device,
        buffer_manager,
        icore_actor.getImkSessionGroups()[i],
        image_make_base_addr_group[i]);
#elif defined(USE_ZG330_BACKEND)
    camera_actor = std::make_unique<SDICameraInputActor<FPAIDevice, FPAIBackend>>(
        camera->getSourceId(),
        std::move(camera),
        fpai_device,
        buffer_manager,
        icore_actor.getIcoreSessionGroups()[i], // pass ref of whole icore sessions
        image_make_base_addr_group[i]);
#endif
    camera_actor->bindOutputQueue(&icore_input_queue);

    // 创建用于yolov5的后处理函数“适配器”lambda， 签名完美匹配 PostProcessingFunc
    HDMIDisplayActor<FPAIDevice, IcoreMessageForPost>::PostProcessingFunc yolov5_post_processor =
[&](const IcoreMessageForPost &post_msg, cv::Mat &cvmat_to_draw)
{
    if (post_msg.icore_tensors.empty()) return;

    int source_id = post_msg.meta.source_id;
    auto &last_results = last_results_group[source_id];
    auto &fps_calc = fps_calculators[source_id];

    fps_calc.tick();

    YoloPostResult post_results;
    if (netinfos[source_id].DetPost_on) {
        post_results = post_detpost_plin(post_msg.icore_tensors, last_results, netinfos[source_id],
            yolov5_cfg.CONF, yolov5_cfg.IOU_THRESHOLD, yolov5_cfg.MULTILABEL,
            yolov5_cfg.FPGA_NMS, yolov5_cfg.N_CLASS, yolov5_cfg.ANCHORS, device);
    } else {
        post_results = post_detpost_soft(post_msg.icore_tensors, last_results, yolov5_cfg.LABELS,
            yolov5_cfg.ANCHORS, netinfos[source_id], yolov5_cfg.N_CLASS, yolov5_cfg.CONF, yolov5_cfg.IOU_THRESHOLD);
    }

    last_results = post_results;

    std::vector<int> id_list = std::get<0>(post_results);
    std::vector<float> score_list = std::get<1>(post_results);
    std::vector<cv::Rect2f> box_list = std::get<2>(post_results);

    // 保留原始检测结果供实测诊断使用；后续控制逻辑仍然只使用 bicycle。
    const std::vector<int> raw_id_list = id_list;
    const std::vector<float> raw_score_list = score_list;
    const std::vector<cv::Rect2f> raw_box_list = box_list;

    // 过滤自行车
    std::vector<int> filtered_id_list;
    std::vector<float> filtered_score_list;
    std::vector<cv::Rect2f> filtered_box_list;
    for (size_t i = 0; i < id_list.size(); ++i)
        if (yolov5_cfg.LABELS[id_list[i]] == "bicycle") {
            filtered_id_list.push_back(id_list[i]);
            filtered_score_list.push_back(score_list[i]);
            filtered_box_list.push_back(box_list[i]);
        }
    id_list = filtered_id_list;
    score_list = filtered_score_list;
    box_list = filtered_box_list;

    if (DEBUG_PRINT_RAW_DETECTION_SUMMARY) {
        static int debug_detection_frame_count = 0;
        ++debug_detection_frame_count;
        if (debug_detection_frame_count % 30 == 1) {
            std::cout << "[YOLO DEBUG] raw=" << raw_box_list.size()
                      << ", bicycle=" << box_list.size();
            const size_t debug_print_count = std::min<size_t>(raw_box_list.size(), 5);
            for (size_t i = 0; i < debug_print_count; ++i) {
                const int cls_id = raw_id_list[i];
                const std::string cls_name =
                    (cls_id >= 0 && cls_id < static_cast<int>(yolov5_cfg.LABELS.size()))
                        ? yolov5_cfg.LABELS[cls_id]
                        : "unknown";
                std::cout << " | " << cls_name
                          << ":" << static_cast<int>(raw_score_list[i] * 100) << "%"
                          << " w=" << raw_box_list[i].width
                          << " h=" << raw_box_list[i].height;
            }
            std::cout << std::endl;
        }
    }

    // Target selection is done after map_box_to_display(), so aiming, distance,
    // HDMI drawing, and target continuity all use the same coordinate system.
    int target_index = -1;

    static float last_target_x = -1, last_target_y = -1;
    static int last_motor1 = 0, last_motor2 = 0;

    // 新增：用于“底盘稳定后再解算 pitch”
    static int stable_frame_count = 0;
    static bool pitch_command_sent = false;

    int send_yaw = CENTER_YAW;
    int send_pitch = last_pitch;

    std::tuple<bool, float, float, int, int> ratio_bias = camera_actor->getRatioBias();
    bool is_hw_resize = std::get<0>(ratio_bias);
    float RATIO_W = std::get<1>(ratio_bias);
    float RATIO_H = std::get<2>(ratio_bias);
    int BIAS_W = std::get<3>(ratio_bias);
    int BIAS_H = std::get<4>(ratio_bias);

    auto map_box_to_display = [&](const cv::Rect2f &box) -> cv::Rect2f {
        // Map model-space boxes back to HDMI/display coordinates. The same
        // mapped box is used for drawing, distance estimation, and control.
        if (is_hw_resize) {
            return cv::Rect2f(
                box.tl().x * RATIO_W + BIAS_W,
                box.tl().y * RATIO_H + BIAS_H,
                box.width * RATIO_W,
                box.height * RATIO_H
            );
        }

        return cv::Rect2f(
            (box.tl().x - BIAS_W) / RATIO_W,
            (box.tl().y - BIAS_H) / RATIO_H,
            box.width / RATIO_W,
            box.height / RATIO_H
        );
    };

    auto display_box_to_model = [&](const cv::Rect2f &display_box) -> cv::Rect2f {
        if (is_hw_resize) {
            return cv::Rect2f(
                (display_box.tl().x - BIAS_W) / RATIO_W,
                (display_box.tl().y - BIAS_H) / RATIO_H,
                display_box.width / RATIO_W,
                display_box.height / RATIO_H
            );
        }

        return cv::Rect2f(
            display_box.tl().x * RATIO_W + BIAS_W,
            display_box.tl().y * RATIO_H + BIAS_H,
            display_box.width * RATIO_W,
            display_box.height * RATIO_H
        );
    };

    if (is_synthetic_target_enabled()) {
        static int synthetic_frame_count = 0;
        ++synthetic_frame_count;

        const int bicycle_id = [&]() {
            for (int idx = 0; idx < static_cast<int>(yolov5_cfg.LABELS.size()); ++idx) {
                if (yolov5_cfg.LABELS[idx] == "bicycle") {
                    return idx;
                }
            }
            return 1;
        }();

        const int phase = (synthetic_frame_count / 45) % 4;
        float synthetic_distance_m = AIM_FOLLOW_TARGET_DISTANCE_M;
        float center_x = yolov5_cfg.FRAME_W * 0.5f;
        float center_y = yolov5_cfg.FRAME_H * 0.5f;

        if (phase == 0) {
            synthetic_distance_m = 1.60f;
        } else if (phase == 1) {
            synthetic_distance_m = 1.00f;
            center_x = yolov5_cfg.FRAME_W * 0.72f;
        } else if (phase == 2) {
            synthetic_distance_m = 1.00f;
            center_y = yolov5_cfg.FRAME_H * 0.30f;
        } else {
            synthetic_distance_m = 0.55f;
        }

        const float display_w = std::clamp(
            DISTANCE_TARGET_REAL_WIDTH_M * DISTANCE_CAMERA_FOCAL_PX / synthetic_distance_m,
            40.0f,
            yolov5_cfg.FRAME_W * 0.55f);
        const float display_h = std::clamp(display_w * 0.75f, 30.0f, yolov5_cfg.FRAME_H * 0.55f);
        const cv::Rect2f display_box(
            std::clamp(center_x - display_w * 0.5f, 0.0f, yolov5_cfg.FRAME_W - display_w),
            std::clamp(center_y - display_h * 0.5f, 0.0f, yolov5_cfg.FRAME_H - display_h),
            display_w,
            display_h);

        id_list.clear();
        score_list.clear();
        box_list.clear();
        id_list.push_back(bicycle_id);
        score_list.push_back(0.99f);
        box_list.push_back(display_box_to_model(display_box));

        std::cout << "[SYNTHETIC TARGET] phase=" << phase
                  << " distance=" << synthetic_distance_m
                  << " display_box=" << display_box.x << "," << display_box.y
                  << "," << display_box.width << "," << display_box.height
                  << std::endl;
    }

    static aim_follow::TargetSelector target_selector;
    static bool target_selector_configured = false;
    if (!target_selector_configured) {
        aim_follow::TargetSelectorConfig selector_cfg;
        selector_cfg.frame_width = static_cast<float>(yolov5_cfg.FRAME_W);
        selector_cfg.frame_height = static_cast<float>(yolov5_cfg.FRAME_H);
        selector_cfg.max_center_jump_norm = AIM_FOLLOW_SELECTOR_MAX_CENTER_JUMP_NORM;
        selector_cfg.area_switch_ratio = AIM_FOLLOW_SELECTOR_AREA_SWITCH_RATIO;
        selector_cfg.max_lost_frames = AIM_FOLLOW_SELECTOR_MAX_LOST_FRAMES;
        target_selector.setConfig(selector_cfg);
        std::cout << "[AIM FOLLOW CONFIG] selector max_center_jump_norm="
                  << selector_cfg.max_center_jump_norm
                  << " area_switch_ratio=" << selector_cfg.area_switch_ratio
                  << " max_lost_frames=" << selector_cfg.max_lost_frames
                  << std::endl;
        target_selector_configured = true;
    }

    std::vector<aim_follow::TargetCandidate> target_candidates;
    target_candidates.reserve(box_list.size());
    for (int i = 0; i < static_cast<int>(box_list.size()); ++i) {
        const cv::Rect2f display_box = map_box_to_display(box_list[i]);
        aim_follow::TargetCandidate candidate;
        candidate.index = i;
        candidate.center_x = display_box.x + display_box.width * 0.5f;
        candidate.center_y = display_box.y + display_box.height * 0.5f;
        candidate.area = display_box.width * display_box.height;
        candidate.score = score_list[i];
        target_candidates.push_back(candidate);
    }
    target_index = target_selector.select(target_candidates);

    static aim_follow::AimFollowController follow_controller;
    static bool follow_controller_configured = false;
    if (!follow_controller_configured) {
        aim_follow::ControlConfig follow_cfg;
        follow_cfg.frame_width = static_cast<float>(yolov5_cfg.FRAME_W);
        follow_cfg.frame_height = static_cast<float>(yolov5_cfg.FRAME_H);
        follow_cfg.center_yaw = CENTER_YAW;
        follow_cfg.center_pitch = CENTER_PITCH;
        follow_cfg.min_yaw = CMD_YAW_MIN;
        follow_cfg.max_yaw = CMD_YAW_MAX;
        follow_cfg.min_pitch = CMD_PITCH_MIN;
        follow_cfg.max_pitch = CMD_PITCH_MAX;
        follow_cfg.motor_rpm_min = MOTOR_RPM_MIN;
        follow_cfg.motor_rpm_max = MOTOR_RPM_MAX;
        follow_cfg.target_distance_m = AIM_FOLLOW_TARGET_DISTANCE_M;
        follow_cfg.yaw_kp = AIM_FOLLOW_YAW_KP;
        follow_cfg.yaw_kd = AIM_FOLLOW_YAW_KD;
        follow_cfg.pitch_kp = AIM_FOLLOW_PITCH_KP;
        follow_cfg.pitch_kd = AIM_FOLLOW_PITCH_KD;
        follow_cfg.invert_yaw = AIM_FOLLOW_INVERT_YAW;
        follow_cfg.invert_pitch = AIM_FOLLOW_INVERT_PITCH;
        follow_cfg.aim_deadzone_norm = AIM_FOLLOW_AIM_DEADZONE_NORM;
        follow_cfg.max_cmd_step = AIM_FOLLOW_MAX_CMD_STEP;
        follow_cfg.distance_deadband_m = AIM_FOLLOW_DISTANCE_DEADBAND_M;
        follow_cfg.follow_kp_rpm_per_m = AIM_FOLLOW_FOLLOW_KP_RPM_PER_M;
        follow_cfg.min_follow_rpm = AIM_FOLLOW_MIN_FOLLOW_RPM;
        follow_cfg.max_follow_rpm = AIM_FOLLOW_MAX_FOLLOW_RPM;
        follow_cfg.motor1_forward_sign = AIM_FOLLOW_MOTOR1_FORWARD_SIGN;
        follow_cfg.motor2_forward_sign = AIM_FOLLOW_MOTOR2_FORWARD_SIGN;
        follow_cfg.lost_hold_frames = AIM_FOLLOW_LOST_HOLD_FRAMES;
        follow_controller.setConfig(follow_cfg);
        std::cout << "[AIM FOLLOW CONFIG] target_distance_m="
                  << follow_cfg.target_distance_m
                  << " yaw_kp=" << follow_cfg.yaw_kp
                  << " yaw_kd=" << follow_cfg.yaw_kd
                  << " pitch_kp=" << follow_cfg.pitch_kp
                  << " pitch_kd=" << follow_cfg.pitch_kd
                  << " invert_yaw=" << follow_cfg.invert_yaw
                  << " invert_pitch=" << follow_cfg.invert_pitch
                  << " distance_deadband_m=" << follow_cfg.distance_deadband_m
                  << " follow_kp_rpm_per_m=" << follow_cfg.follow_kp_rpm_per_m
                  << " min_follow_rpm=" << follow_cfg.min_follow_rpm
                  << " max_follow_rpm=" << follow_cfg.max_follow_rpm
                  << " motor_signs=" << follow_cfg.motor1_forward_sign
                  << "," << follow_cfg.motor2_forward_sign
                  << " lost_hold_frames=" << follow_cfg.lost_hold_frames
                  << std::endl;
        follow_controller_configured = true;
    }

    int filtered_target_index = -1;
    float target_raw_distance_m = -1.0f;
    float target_filtered_distance_m = -1.0f;
    bool control_target_valid = false;
    float control_distance_error_m = 0.0f;
    float control_ex = 0.0f;
    float control_ey = 0.0f;
    int control_motor1 = last_motor1;
    int control_motor2 = last_motor2;
    int control_pitch = last_pitch;
    int control_yaw = last_yaw;

    if (target_index >= 0) {
        float x1, y1, w, h;

        if (is_hw_resize) {
            x1 = box_list[target_index].tl().x * RATIO_W + BIAS_W;
            y1 = box_list[target_index].tl().y * RATIO_H + BIAS_H;
            w  = box_list[target_index].width  * RATIO_W;
            h  = box_list[target_index].height * RATIO_H;
        } else {
            x1 = (box_list[target_index].tl().x - BIAS_W) / RATIO_W;
            y1 = (box_list[target_index].tl().y - BIAS_H) / RATIO_H;
            w  = box_list[target_index].width  / RATIO_W;
            h  = box_list[target_index].height / RATIO_H;
        }

        float cx = x1 + w / 2.0f;
        float cy = y1 + h / 2.0f;

            // 新增打印中心点信息
        std::cout << "ok, center=(" << cx << ", " << cy << ")" << std::endl;
        // 检测目标移动
        const float MOVE_THRESHOLD = 5.f;
        float dx = cx - last_target_x;
        float dy = cy - last_target_y;
        bool target_moved = (std::abs(dx) > MOVE_THRESHOLD) || (std::abs(dy) > MOVE_THRESHOLD);
        (void)target_moved;
        last_target_x = cx;
        last_target_y = cy;

// Aim-follow module: gimbal aiming + fixed-distance chassis follow.
        target_raw_distance_m = estimate_distance_m(w);
        target_filtered_distance_m = filter_distance_m(target_raw_distance_m);
        filtered_target_index = target_index;

        aim_follow::TargetObservation follow_obs;
        follow_obs.valid = true;
        follow_obs.center_x = cx;
        follow_obs.center_y = cy;
        follow_obs.box_width = w;
        follow_obs.distance_m = target_filtered_distance_m;
        follow_obs.timestamp_s = std::chrono::duration<float>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        const auto follow_cmd = AIM_FOLLOW_CONTROL_ENABLE
            ? follow_controller.update(follow_obs)
            : aim_follow::ControlOutput{};

        int motor1_rpm = follow_cmd.motor1_rpm;
        int motor2_rpm = follow_cmd.motor2_rpm;
        send_pitch = follow_cmd.pitch;
        send_yaw = follow_cmd.yaw;
        control_target_valid = true;
        control_distance_error_m = follow_cmd.distance_error_m;
        control_ex = follow_cmd.norm_error_x;
        control_ey = follow_cmd.norm_error_y;
        control_motor1 = motor1_rpm;
        control_motor2 = motor2_rpm;
        control_pitch = send_pitch;
        control_yaw = send_yaw;

        std::cout << "[AIM FOLLOW] cx=" << cx
                  << " cy=" << cy
                  << " raw_distance=" << target_raw_distance_m
                  << " distance=" << target_filtered_distance_m
                  << " dist_error=" << follow_cmd.distance_error_m
                  << " ex=" << follow_cmd.norm_error_x
                  << " ey=" << follow_cmd.norm_error_y
                  << " motor1=" << motor1_rpm
                  << " motor2=" << motor2_rpm
                  << " pitch=" << send_pitch
                  << " yaw=" << send_yaw
                  << std::endl;

        auto now = std::chrono::steady_clock::now();
        static auto last_can_time = std::chrono::steady_clock::now();
        const auto CAN_INTERVAL = 50ms;

        if (now - last_can_time >= CAN_INTERVAL)
        {
            send_chassis_can_mode(motor1_rpm, motor2_rpm, send_pitch, send_yaw, TRIGGER_STOP, CAN_CONTROL_ENABLE);
            send_gimbal_can_mode(send_pitch, send_yaw, TRIGGER_STOP);
            last_motor1 = motor1_rpm;
            last_motor2 = motor2_rpm;
            last_yaw = send_yaw;
            last_pitch = send_pitch;
            last_can_time = now;
        }
    }else {
        auto now = std::chrono::steady_clock::now();
        static auto last_lost_can_time = std::chrono::steady_clock::now();
        const auto LOST_CAN_INTERVAL = 100ms;

        aim_follow::TargetObservation lost_obs;
        lost_obs.valid = false;
        lost_obs.timestamp_s = std::chrono::duration<float>(
            now.time_since_epoch()).count();
        const auto lost_cmd = AIM_FOLLOW_CONTROL_ENABLE
            ? follow_controller.update(lost_obs)
            : aim_follow::ControlOutput{};
        control_target_valid = false;
        control_distance_error_m = lost_cmd.distance_error_m;
        control_ex = lost_cmd.norm_error_x;
        control_ey = lost_cmd.norm_error_y;
        control_motor1 = lost_cmd.motor1_rpm;
        control_motor2 = lost_cmd.motor2_rpm;
        control_pitch = lost_cmd.pitch;
        control_yaw = lost_cmd.yaw;

        stable_frame_count = 0;
        pitch_command_sent = false;

        if (now - last_lost_can_time >= LOST_CAN_INTERVAL) {
            send_chassis_can_mode(lost_cmd.motor1_rpm, lost_cmd.motor2_rpm,
                                  lost_cmd.pitch, lost_cmd.yaw,
                                  TRIGGER_STOP, CAN_CONTROL_ENABLE);
            send_gimbal_can_mode(lost_cmd.pitch, lost_cmd.yaw, TRIGGER_STOP);
            last_motor1 = lost_cmd.motor1_rpm;
            last_motor2 = lost_cmd.motor2_rpm;
            last_yaw = lost_cmd.yaw;
            last_pitch = lost_cmd.pitch;
            last_lost_can_time = now;
        }
    }

    // 绘制检测框
    const int panel_x = 18;
    const int panel_y = 56;
    const int panel_w = 690;
    const int panel_h = 168;
    cv::rectangle(cvmat_to_draw,
                  cv::Rect(panel_x - 10, panel_y - 34, panel_w, panel_h),
                  cv::Scalar(0, 0, 0), cv::FILLED);

    auto draw_control_text = [&](const std::string &text, int row, const cv::Scalar &color) {
        const cv::Point org(panel_x, panel_y + row * 34);
        cv::putText(cvmat_to_draw, text, org, cv::FONT_HERSHEY_DUPLEX, 0.8,
                    cv::Scalar(0, 0, 0), 4, cv::LINE_AA);
        cv::putText(cvmat_to_draw, text, org, cv::FONT_HERSHEY_DUPLEX, 0.8,
                    color, 1, cv::LINE_AA);
    };

    const std::string target_state = control_target_valid ? "LOCK" : "LOST";
    const std::string distance_text = target_filtered_distance_m > 0.0f
        ? fmt::format("{:.2f}m", target_filtered_distance_m)
        : "--";
    draw_control_text(fmt::format("Target:{}  Distance:{}  Error:{:+.2f}m",
                                  target_state, distance_text, control_distance_error_m),
                      0, control_target_valid ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 255, 255));
    draw_control_text(fmt::format("Gimbal tracking: pitch={} yaw={}  ex={:+.2f} ey={:+.2f}",
                                  control_pitch, control_yaw, control_ex, control_ey),
                      1, cv::Scalar(255, 255, 255));
    draw_control_text(fmt::format("Chassis tracking: motor1={}rpm motor2={}rpm",
                                  control_motor1, control_motor2),
                      2, cv::Scalar(255, 255, 255));
    draw_control_text(fmt::format("CAN output: {}",
                                  is_can_dry_run_enabled() ? "DRYRUN(no write)" : "ACTIVE(write can0)"),
                      3, is_can_dry_run_enabled() ? cv::Scalar(0, 255, 255) : cv::Scalar(0, 0, 255));

    for (int i = 0; i < box_list.size(); ++i) {
        const cv::Rect2f display_box = map_box_to_display(box_list[i]);
        cv::rectangle(cvmat_to_draw, display_box, classColor(id_list[i]), BOX_THICKNESS, cv::LINE_AA);
        const std::string class_label =
            yolov5_cfg.LABELS[id_list[i]] + ":" + std::to_string(int(score_list[i]*100)) + "%";
        // 计算并显示目标距离
        const float distance_m = estimate_distance_m(display_box.width);
        const cv::Point2f label_origin = display_box.tl() - cv::Point2f(0, 5);
        cv::putText(cvmat_to_draw, class_label, label_origin, cv::FONT_HERSHEY_DUPLEX,
                    1.0, cv::Scalar(0, 0, 0), 3);
        cv::putText(cvmat_to_draw, class_label, label_origin, cv::FONT_HERSHEY_DUPLEX,
                    1.0, cv::Scalar(255, 255, 255), 1);
        if (distance_m > 0.0f) {
            const bool is_follow_target = (i == filtered_target_index) && (target_filtered_distance_m > 0.0f);
            const float display_distance_m = is_follow_target ? target_filtered_distance_m : distance_m;
            const std::string distance_label = fmt::format("D:{:.2f}m", display_distance_m);
            const int distance_x = static_cast<int>(display_box.tl().x);
            const int distance_y = std::max(35, static_cast<int>(display_box.tl().y) - 35);
            const cv::Point distance_origin(distance_x, distance_y);

            // 距离文字使用红色粗字并加黑色描边，白纸和复杂背景上都更醒目。
            cv::putText(cvmat_to_draw, distance_label, distance_origin, cv::FONT_HERSHEY_DUPLEX,
                        1.2, cv::Scalar(0, 0, 0), 5);
            cv::putText(cvmat_to_draw, distance_label, distance_origin, cv::FONT_HERSHEY_DUPLEX,
                        1.2, cv::Scalar(0, 0, 255), 2);

            std::cout << "[DISTANCE DEBUG] " << yolov5_cfg.LABELS[id_list[i]]
                      << " score=" << static_cast<int>(score_list[i] * 100) << "%"
                      << " box_width=" << display_box.width
                      << " raw_distance_m=" << distance_m
                      << " display_distance_m=" << display_distance_m << std::endl;
        }
    }

    // 如果当前没有 bicycle，额外画出原始 YOLO 检测框，方便实测判断模型是否检测成了别的类别。
    // 这些黄色框只用于显示和保存图片，不参与云台、底盘和距离控制。
    if (DEBUG_DRAW_RAW_DETECTIONS_WHEN_NO_BICYCLE && box_list.empty()) {
        for (size_t i = 0; i < raw_box_list.size(); ++i) {
            const cv::Rect2f display_box = map_box_to_display(raw_box_list[i]);
            const int cls_id = raw_id_list[i];
            const std::string cls_name =
                (cls_id >= 0 && cls_id < static_cast<int>(yolov5_cfg.LABELS.size()))
                    ? yolov5_cfg.LABELS[cls_id]
                    : "unknown";
            cv::rectangle(cvmat_to_draw, display_box, cv::Scalar(0, 255, 255), 1, cv::LINE_AA);
            const std::string label =
                "raw " + cls_name + ":" + std::to_string(static_cast<int>(raw_score_list[i] * 100)) + "%";
            cv::putText(cvmat_to_draw, label, display_box.tl() - cv::Point2f(0, 5),
                        cv::FONT_HERSHEY_DUPLEX, 0.8, cv::Scalar(0, 255, 255), 1);
        }
    }

    drawTextOnTwoCorners(cvmat_to_draw, fmt::format("FPS: {:.1f}", fps_calc.getFPS()), DEMO_NAME, cv::Scalar(0,0,0));

    if (DEBUG_SAVE_FRAME_ENABLE && (!DEBUG_SAVE_DETECTION_ONLY || !box_list.empty())) {
        static int debug_save_frame_count = 0;
        static int debug_saved_frame_count = 0;
        ++debug_save_frame_count;
        if (debug_save_frame_count >= DEBUG_SAVE_FRAME_INTERVAL &&
            debug_saved_frame_count < DEBUG_SAVE_MAX_FRAMES) {
            debug_save_frame_count = 0;
            try {
                fs::create_directories(DEBUG_SAVE_FRAME_DIR);
                cv::Mat debug_save_mat;
                if (cvmat_to_draw.channels() == 2) {
                    cv::cvtColor(cvmat_to_draw, debug_save_mat, cv::COLOR_BGR5652BGR);
                } else {
                    debug_save_mat = cvmat_to_draw;
                }
                cv::imwrite(DEBUG_SAVE_FRAME_PATH, debug_save_mat);
                cv::imwrite(fmt::format("{}/detect_frame_{:03d}.jpg",
                                        DEBUG_SAVE_FRAME_DIR,
                                        debug_saved_frame_count + 1),
                            debug_save_mat);
                ++debug_saved_frame_count;
            } catch (const std::exception &e) {
                std::cerr << "[DEBUG SAVE] save frame failed: " << e.what() << std::endl;
            }
        }
    }
};

    // 配置HDMI输出
    int hdmi_id = 0;
    auto hdmi_display = std::make_unique<RGB565HDMIDisplay<FPAIDevice>>(hdmi_id, fpai_device, FRAME_W, FRAME_H);

    HDMIDisplayActor<FPAIDevice, IcoreMessageForPost> display_actor(hdmi_display->getSinkId(),
                                                                    std::move(hdmi_display),
                                                                    fpai_device,
                                                                    buffer_manager,
                                                                    camera_actor->getChunkGroupId(),
                                                                    yolov5_post_processor);
    display_actor.bindInputQueue(&icore_output_queue);

    // 9. 启动所有Actors

    camera_actor->start();
    display_actor.start();
    icore_actor.start();

    spdlog::info("All actors started...");

    // Blocking loop that waits for the stop signal
    std::cin.get(); // Press Enter to stop

    camera_actor->stop();
    icore_actor.stop();
    display_actor.stop();

    spdlog::info("All actors stopped...");
    if (can_socket >= 0) {
        close(can_socket);
        can_socket = -1;
    }
    Device::Close(device);

    return 0;
}
