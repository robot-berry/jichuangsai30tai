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
#include "aim_follow_controller.hpp"
#include "BYTETracker.h"
#include "ai_example/postprocesses.hpp"
#include "ai_example/yolov5_npu_actor.hpp"

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
#include <cstdlib>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <cerrno>
//加入引用
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
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

// 瞄准镜参数
const bool RETICLE_ENABLE = true;
const int RETICLE_CENTER_X_OFFSET = 0;   // 瞄准镜中心X偏移，可调
const int RETICLE_CENTER_Y_OFFSET = 0;   // 瞄准镜中心Y偏移，可调

const int RETICLE_RADIUS = 80;           // 外圆半径
const int RETICLE_CIRCLE_THICKNESS = 2;  // 外圆线宽
const int RETICLE_LINE_THICKNESS = 2;    // 十字线宽
const int RETICLE_CROSS_HALF = 55;       // 十字线半长

const int RETICLE_GAP = 8;               // 中心空隙半径
const int RETICLE_TICK_STEP = 8;         // 中心刻度间隔
const int RETICLE_TICK_COUNT = 4;        // 中心刻度数量
const int RETICLE_TICK_LEN = 4;          // 中心刻度长度

// 颜色：OpenCV里是 BGR
const cv::Scalar RETICLE_COLOR = cv::Scalar(0, 0, 0);   // 黑色




//--------------------------------------------------------云台控制参数-----------------------------------------------------------------------
const int DEADZONE_INNER = 8;   // 微小抖动允许
const int DEADZONE_OUTER = 6;   // 超过这个才触发移动

const int CENTER_YAW = 123;  // Calibrated vehicle-forward heading.
const int CENTER_PITCH = 150;
const int CMD_YAW_MIN = 100;
const int CMD_YAW_MAX = 200;
const int CMD_PITCH_MIN = 100;
const int CMD_PITCH_MAX = 200;
//--------------------------------------------------------云台控制参数-----------------------------------------------------------------------
static int last_yaw   = CENTER_YAW;
static int last_pitch = CENTER_PITCH;
static int last_motor1 = 0;
static int last_motor2 = 0;

const bool AIM_FOLLOW_CONTROL_ENABLE = true;
const float AIM_FOLLOW_TARGET_DISTANCE_M = 1.0f;
const float AIM_FOLLOW_YAW_KP = 38.0f;
const float AIM_FOLLOW_YAW_KD = 8.0f;
const float AIM_FOLLOW_PITCH_KP = 42.0f;
const float AIM_FOLLOW_PITCH_KD = 8.0f;
const bool AIM_FOLLOW_INVERT_YAW = false;
const bool AIM_FOLLOW_INVERT_PITCH = false;
const float AIM_FOLLOW_AIM_DEADZONE_NORM = 0.035f;
const float AIM_FOLLOW_MAX_CMD_STEP = 8.0f;
const float AIM_FOLLOW_DISTANCE_DEADBAND_M = 0.01f;
const float AIM_FOLLOW_DISTANCE_RESUME_DEADBAND_M = 0.05f;
const float AIM_FOLLOW_FOLLOW_KP_RPM_PER_M = 180.0f;
const int AIM_FOLLOW_MIN_FOLLOW_RPM = 35;
const int AIM_FOLLOW_MAX_FOLLOW_RPM = 160;
const int AIM_FOLLOW_MOTOR1_FORWARD_SIGN = 1;
const int AIM_FOLLOW_MOTOR2_FORWARD_SIGN = 1;
const bool AIM_FOLLOW_DISTANCE_ENABLE = true;
const bool AIM_FOLLOW_CHASSIS_STEER_ENABLE = false;
const float AIM_FOLLOW_STEER_DEADZONE_NORM = 0.12f;
const float AIM_FOLLOW_STEER_KP_RPM = 90.0f;
const int AIM_FOLLOW_MIN_STEER_RPM = 25;
const int AIM_FOLLOW_MAX_STEER_RPM = 45;
const int AIM_FOLLOW_MOTOR1_STEER_SIGN = 1;
const int AIM_FOLLOW_MOTOR2_STEER_SIGN = -1;
const int AIM_FOLLOW_LOST_HOLD_FRAMES = 5;
const bool AIM_FOLLOW_SEARCH_ENABLE = true;
const int AIM_FOLLOW_SEARCH_RPM = 40;
const int AIM_FOLLOW_SEARCH_SWEEP_FRAMES = 60;
const int AIM_FOLLOW_DEFAULT_SEARCH_DIRECTION = -1;
const float AIM_FOLLOW_SELECTOR_MAX_CENTER_JUMP_NORM = 0.25f;
const float AIM_FOLLOW_SELECTOR_AREA_SWITCH_RATIO = 1.8f;
const int AIM_FOLLOW_SELECTOR_MAX_LOST_FRAMES = 5;
const bool AIM_FOLLOW_BYTETRACK_ENABLE = true;
const int AIM_FOLLOW_BYTETRACK_FRAME_RATE = 30;
const int AIM_FOLLOW_BYTETRACK_BUFFER_FRAMES = 90;
const int AIM_FOLLOW_BYTETRACK_SWITCH_DELAY_FRAMES = 3;
const float AIM_FOLLOW_BYTETRACK_TRACK_THRESH = 0.30f;
const float AIM_FOLLOW_BYTETRACK_HIGH_THRESH = 0.45f;
const float AIM_FOLLOW_BYTETRACK_MATCH_THRESH = 0.80f;
const bool AIM_FOLLOW_LASER_AIM_ENABLE = false;
const float AIM_FOLLOW_LASER_CENTER_GATE_NORM = 0.08f;
const int AIM_FOLLOW_LASER_RED_MIN = 250;
const int AIM_FOLLOW_LASER_RED_DOMINANCE = 130;
const int AIM_FOLLOW_LASER_REFLECTION_MAX = 200;
const int AIM_FOLLOW_LASER_LOCAL_CONTRAST = 20;
const int AIM_FOLLOW_LASER_MIN_AREA = 2;
const int AIM_FOLLOW_LASER_MAX_AREA = 220;
const int AIM_FOLLOW_LASER_MAX_SPAN = 36;
const bool AIM_FOLLOW_LASER_DEBUG_VIEW = false;
const float AIM_FOLLOW_LASER_DEBUG_GAIN = 4.0f;
const bool AIM_FOLLOW_LASER_MOTION_ENABLE = true;
const int AIM_FOLLOW_LASER_MOTION_DELTA_MIN = 6;
const int AIM_FOLLOW_LASER_MOTION_LOCAL_MIN = 3;
const int AIM_FOLLOW_LASER_MOTION_MIN_AREA = 2;
const int AIM_FOLLOW_LASER_MOTION_MAX_AREA = 160;
const int AIM_FOLLOW_LASER_MOTION_MAX_SPAN = 24;
const int AIM_FOLLOW_LASER_MOTION_SETTLE_FRAMES = 1;
const int AIM_FOLLOW_LASER_MOTION_SAMPLE_FRAMES = 4;
const int AIM_FOLLOW_LASER_MOTION_HOLD_FRAMES = 6;
const bool AIM_FOLLOW_LASER_COARSE_YAW_ENABLE = false;
const bool AIM_FOLLOW_LASER_FINE_YAW_ENABLE = true;
const float DISTANCE_TARGET_REAL_WIDTH_M = 0.24f;
// This focal length was calibrated in YOLO model coordinates (640 px wide),
// not the 1920 px HDMI display coordinates.
const float DISTANCE_CAMERA_FOCAL_PX = 600.0f;
const float DISTANCE_MIN_BOX_WIDTH_PX = 1.0f;
const float DISTANCE_FILTER_ALPHA = 0.18f;
const int DISTANCE_MEDIAN_WINDOW_SIZE = 5;
const float DISTANCE_STABILITY_DEADBAND_M = 0.01f;
const float DISTANCE_MAX_FILTERED_STEP_M = 0.12f;
const int TRIGGER_STOP = 0;
const int CHASSIS_GIMBAL_NEUTRAL = 100;
const int GIMBAL_AUX_CENTER = 150;
const int GIMBAL_COMMAND_REPEAT = 3;
const bool CAN_CONTROL_ENABLE = true;
const char *AIM_FOLLOW_CAN_DRYRUN_ENV = "AIM_FOLLOW_CAN_DRYRUN";
const char *AIM_FOLLOW_SYNTHETIC_TARGET_ENV = "AIM_FOLLOW_SYNTHETIC_TARGET";
const char *AIM_FOLLOW_SETUP_CAN_ENV = "AIM_FOLLOW_SETUP_CAN";
const char *AIM_FOLLOW_CAN_BITRATE_ENV = "AIM_FOLLOW_CAN_BITRATE";
const char *AIM_FOLLOW_GIMBAL_ENABLE_ENV = "AIM_FOLLOW_GIMBAL_ENABLE";
const char *AIM_FOLLOW_CHASSIS_ENABLE_ENV = "AIM_FOLLOW_CHASSIS_ENABLE";



//-------------------------------------------------------加入CAN函数----------------------------------------------------------------
int can_socket = -1;
int can_lock_fd = -1;
const char *CAN_WRITER_LOCK_PATH = "/tmp/plin_aim_follow_can.lock";

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

bool get_env_bool(const char *name, bool default_value) {
    const char *value = std::getenv(name);
    if (value == nullptr) {
        return default_value;
    }
    if (std::strcmp(value, "1") == 0 ||
        std::strcmp(value, "true") == 0 ||
        std::strcmp(value, "TRUE") == 0 ||
        std::strcmp(value, "yes") == 0 ||
        std::strcmp(value, "YES") == 0) {
        return true;
    }
    if (std::strcmp(value, "0") == 0 ||
        std::strcmp(value, "false") == 0 ||
        std::strcmp(value, "FALSE") == 0 ||
        std::strcmp(value, "no") == 0 ||
        std::strcmp(value, "NO") == 0) {
        return false;
    }
    std::cerr << "[AIM FOLLOW CONFIG] invalid bool env " << name
              << "=" << value << ", keep default=" << default_value << std::endl;
    return default_value;
}

float get_env_float(const char *name, float default_value, float min_value, float max_value) {
    const char *value = std::getenv(name);
    if (value == nullptr) {
        return default_value;
    }

    errno = 0;
    char *end = nullptr;
    const float parsed = std::strtof(value, &end);
    if (end == value || errno != 0 || !std::isfinite(parsed)) {
        std::cerr << "[AIM FOLLOW CONFIG] invalid float env " << name
                  << "=" << value << ", keep default=" << default_value << std::endl;
        return default_value;
    }
    return std::clamp(parsed, min_value, max_value);
}

int get_env_int(const char *name, int default_value, int min_value, int max_value) {
    const char *value = std::getenv(name);
    if (value == nullptr) {
        return default_value;
    }

    errno = 0;
    char *end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || errno != 0) {
        std::cerr << "[AIM FOLLOW CONFIG] invalid int env " << name
                  << "=" << value << ", keep default=" << default_value << std::endl;
        return default_value;
    }
    return std::clamp(static_cast<int>(parsed), min_value, max_value);
}

bool is_can_dry_run_enabled() {
    return is_env_enabled(AIM_FOLLOW_CAN_DRYRUN_ENV);
}

bool is_synthetic_target_enabled() {
    return is_env_enabled(AIM_FOLLOW_SYNTHETIC_TARGET_ENV);
}

void close_can_socket() {
    if (can_socket >= 0) {
        close(can_socket);
        can_socket = -1;
    }
    if (can_lock_fd >= 0) {
        flock(can_lock_fd, LOCK_UN);
        close(can_lock_fd);
        can_lock_fd = -1;
    }
}

void setup_can0_if_requested() {
    if (!is_env_enabled(AIM_FOLLOW_SETUP_CAN_ENV)) {
        return;
    }

    const int bitrate = get_env_int(AIM_FOLLOW_CAN_BITRATE_ENV, 250000, 10000, 1000000);
    std::ostringstream cmd;
    cmd << "ip link set can0 down 2>/dev/null; "
        << "ip link set can0 type can bitrate " << bitrate << " restart-ms 100; "
        << "ip link set can0 up";
    const int ret = std::system(cmd.str().c_str());
    std::cout << "[CAN SETUP] can0 bitrate=" << bitrate << " ret=" << ret << std::endl;
}

void can_init() {
    if (is_can_dry_run_enabled()) {
        std::cout << "[CAN DRYRUN] AIM_FOLLOW_CAN_DRYRUN=1, skip can0 open/write." << std::endl;
        return;
    }

    can_lock_fd = open(CAN_WRITER_LOCK_PATH, O_CREAT | O_RDWR, 0666);
    if (can_lock_fd < 0 || flock(can_lock_fd, LOCK_EX | LOCK_NB) < 0) {
        std::cerr << "[CAN SAFETY] another CAN writer owns " << CAN_WRITER_LOCK_PATH
                  << "; CAN output remains disabled" << std::endl;
        if (can_lock_fd >= 0) {
            close(can_lock_fd);
            can_lock_fd = -1;
        }
        return;
    }
    ftruncate(can_lock_fd, 0);
    const std::string owner = std::to_string(static_cast<long long>(getpid())) + "\n";
    write(can_lock_fd, owner.data(), owner.size());
    std::cout << "[CAN SAFETY] exclusive writer lock acquired pid=" << getpid() << std::endl;

    setup_can0_if_requested();

    struct sockaddr_can addr;
    struct ifreq ifr;

    can_socket = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (can_socket < 0) {
        perror("socket");
        return;
    }

    std::strcpy(ifr.ifr_name, "can0");
    if (ioctl(can_socket, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl");
        close_can_socket();
        return;
    }

    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(can_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close_can_socket();
        return;
    }

    std::cout << "CAN init ok" << std::endl;
}

void send_can(int pitch, int yaw) {
    if (can_socket < 0) return;

    struct can_frame frame;
    frame.can_id = 0x38A;
    frame.can_dlc = 8;

    frame.data[0] = 0xAA;
    frame.data[1] = static_cast<uint8_t>(pitch);
    frame.data[2] = static_cast<uint8_t>(yaw);
    frame.data[3] = 0xAA;
    frame.data[4] = 0x00;
    frame.data[5] = 0x00;
    frame.data[6] = 0x00;
    frame.data[7] = 0x55;

    std::cout << "[CAN SEND] pitch=" << pitch << " yaw=" << yaw << std::endl;

    ssize_t nbytes = write(can_socket, &frame, sizeof(frame));
    if (nbytes != sizeof(frame)) {
        std::cerr << "CAN send failed" << std::endl;
    }
}

bool send_can_frame(const char *tag, const struct can_frame &frame) {
    if (is_can_dry_run_enabled()) {
        std::ostringstream oss;
        oss << tag << " DRYRUN id=0x" << std::hex << std::uppercase << frame.can_id
            << std::dec << " dlc=" << static_cast<int>(frame.can_dlc) << " data=";
        for (int i = 0; i < frame.can_dlc; ++i) {
            if (i > 0) {
                oss << ' ';
            }
            oss << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                << static_cast<int>(frame.data[i]);
        }
        std::cout << oss.str() << std::dec << std::setfill(' ') << std::endl;
        return true;
    }

    if (can_socket < 0) {
        std::cerr << tag << " CAN socket is not open" << std::endl;
        return false;
    }

    const ssize_t nbytes = write(can_socket, &frame, sizeof(frame));
    if (nbytes != sizeof(frame)) {
        std::cerr << tag << " CAN send failed";
        if (nbytes < 0) {
            std::cerr << ": " << std::strerror(errno);
        } else {
            std::cerr << ": short write " << nbytes << "/" << sizeof(frame);
        }
        std::cerr << std::endl;
        return false;
    }
    return true;
}

bool send_gimbal_can_mode(int pitch, int yaw, int aux) {
    struct can_frame frame;
    std::memset(&frame, 0, sizeof(frame));
    frame.can_id = 0x38A;
    frame.can_dlc = 8;

    frame.data[0] = 0xAA;
    frame.data[1] = static_cast<uint8_t>(std::clamp(pitch, CMD_PITCH_MIN, CMD_PITCH_MAX));
    frame.data[2] = static_cast<uint8_t>(std::clamp(yaw, CMD_YAW_MIN, CMD_YAW_MAX));
    frame.data[3] = static_cast<uint8_t>(std::clamp(aux, CMD_PITCH_MIN, CMD_PITCH_MAX));
    frame.data[4] = 0x00;
    frame.data[5] = 0x00;
    frame.data[6] = 0x00;
    frame.data[7] = 0x55;

    return send_can_frame("[GIMBAL CAN]", frame);
}

void put_i16_be(uint8_t *dst, int value) {
    const int16_t v = static_cast<int16_t>(std::clamp(value, -32768, 32767));
    dst[0] = static_cast<uint8_t>((static_cast<uint16_t>(v) >> 8) & 0xff);
    dst[1] = static_cast<uint8_t>(static_cast<uint16_t>(v) & 0xff);
}

bool send_chassis_can_mode(int motor1_rpm,
                           int motor2_rpm,
                           int pitch,
                           int yaw,
                           int trigger,
                           bool enable) {
    struct can_frame frame;
    std::memset(&frame, 0, sizeof(frame));
    frame.can_id = 0x201;
    frame.can_dlc = 8;

    put_i16_be(&frame.data[0], motor1_rpm);
    put_i16_be(&frame.data[2], motor2_rpm);
    frame.data[4] = static_cast<uint8_t>(std::clamp(pitch, CMD_PITCH_MIN, CMD_PITCH_MAX));
    frame.data[5] = static_cast<uint8_t>(std::clamp(yaw, CMD_YAW_MIN, CMD_YAW_MAX));
    frame.data[6] = static_cast<uint8_t>(std::clamp(trigger, 0, 255));
    frame.data[7] = enable ? 0x01 : 0x00;

    return send_can_frame("[CHASSIS CAN]", frame);
}

void send_gimbal_tracking_command(bool chassis_enabled,
                                  int pitch,
                                  int yaw,
                                  int aux,
                                  int repeat) {
    if (!chassis_enabled) {
        // The vehicle controller only accepts gimbal commands after a zero-speed
        // 0x201 enable heartbeat switches it into CAN control mode.
        send_chassis_can_mode(0,
                              0,
                              CHASSIS_GIMBAL_NEUTRAL,
                              CHASSIS_GIMBAL_NEUTRAL,
                              TRIGGER_STOP,
                              CAN_CONTROL_ENABLE);
    }

    const int send_count = std::clamp(repeat, 1, 8);
    for (int i = 0; i < send_count; ++i) {
        send_gimbal_can_mode(pitch, yaw, aux);
    }
}




//-------------------------------------------------------加入CAN函数----------------------------------------------------------------
//-------------------------------------------------------加入瞄准镜绘制函数----------------------------------------------------------------
void draw_reticle(cv::Mat& img,
                  int center_x,
                  int center_y,
                  int radius,
                  int circle_thickness,
                  int line_thickness,
                  int cross_half,
                  int gap,
                  int tick_step,
                  int tick_count,
                  int tick_len,
                  const cv::Scalar& color) {
    // 外圆
    cv::circle(img, cv::Point(center_x, center_y), radius, color, circle_thickness, cv::LINE_AA);

    // 水平十字线（留中心空隙）
    cv::line(img,
             cv::Point(center_x - cross_half, center_y),
             cv::Point(center_x - gap, center_y),
             color, line_thickness, cv::LINE_AA);

    cv::line(img,
             cv::Point(center_x + gap, center_y),
             cv::Point(center_x + cross_half, center_y),
             color, line_thickness, cv::LINE_AA);

    // 垂直十字线（留中心空隙）
    cv::line(img,
             cv::Point(center_x, center_y - cross_half),
             cv::Point(center_x, center_y - gap),
             color, line_thickness, cv::LINE_AA);

    cv::line(img,
             cv::Point(center_x, center_y + gap),
             cv::Point(center_x, center_y + cross_half),
             color, line_thickness, cv::LINE_AA);

    // 中心小刻度：竖直方向两侧
    for (int i = 1; i <= tick_count; ++i) {
        int dy = i * tick_step;

        cv::line(img,
                 cv::Point(center_x - tick_len, center_y - dy),
                 cv::Point(center_x + tick_len, center_y - dy),
                 color, 1, cv::LINE_AA);

        cv::line(img,
                 cv::Point(center_x - tick_len, center_y + dy),
                 cv::Point(center_x + tick_len, center_y + dy),
                 color, 1, cv::LINE_AA);
    }

    // 中心小刻度：水平方向两侧
    for (int i = 1; i <= tick_count; ++i) {
        int dx = i * tick_step;

        cv::line(img,
                 cv::Point(center_x - dx, center_y - tick_len),
                 cv::Point(center_x - dx, center_y + tick_len),
                 color, 1, cv::LINE_AA);

        cv::line(img,
                 cv::Point(center_x + dx, center_y - tick_len),
                 cv::Point(center_x + dx, center_y + tick_len),
                 color, 1, cv::LINE_AA);
    }
}
//-------------------------------------------------------加入瞄准镜绘制函数----------------------------------------------------------------






struct RedLaserDetectorConfig {
    int red_min = AIM_FOLLOW_LASER_RED_MIN;
    int red_dominance = AIM_FOLLOW_LASER_RED_DOMINANCE;
    int reflection_max = AIM_FOLLOW_LASER_REFLECTION_MAX;
    int local_contrast = AIM_FOLLOW_LASER_LOCAL_CONTRAST;
    int min_area = AIM_FOLLOW_LASER_MIN_AREA;
    int max_area = AIM_FOLLOW_LASER_MAX_AREA;
    int max_span = AIM_FOLLOW_LASER_MAX_SPAN;
};

struct RedLaserDetection {
    bool valid = false;
    cv::Point2f center;
    cv::Rect bounds;
    int area = 0;
    float score = 0.0f;
    float peak_dominance = 0.0f;
    int peak_red = 0;
    cv::Point peak_point;
};

struct LaserMotionDetectorConfig {
    int delta_min = AIM_FOLLOW_LASER_MOTION_DELTA_MIN;
    int local_min = AIM_FOLLOW_LASER_MOTION_LOCAL_MIN;
    int min_area = AIM_FOLLOW_LASER_MOTION_MIN_AREA;
    int max_area = AIM_FOLLOW_LASER_MOTION_MAX_AREA;
    int max_span = AIM_FOLLOW_LASER_MOTION_MAX_SPAN;
};

cv::Mat red_dominance_image(const cv::Mat &bgr) {
    if (bgr.empty() || bgr.type() != CV_8UC3) {
        return cv::Mat();
    }
    std::vector<cv::Mat> channels;
    cv::split(bgr, channels);
    cv::Mat background;
    cv::Mat dominance;
    cv::max(channels[0], channels[1], background);
    cv::subtract(channels[2], background, dominance);
    return dominance;
}

RedLaserDetection detect_red_laser(const cv::Mat &bgr,
                                   const RedLaserDetectorConfig &cfg,
                                   const cv::Point2f &target_center) {
    RedLaserDetection best;
    if (bgr.empty() || bgr.type() != CV_8UC3) {
        return best;
    }

    std::vector<cv::Mat> channels;
    cv::split(bgr, channels);
    const cv::Mat dominance_image = red_dominance_image(bgr);
    cv::Mat red_mask;
    cv::Mat red_green;
    cv::Mat red_blue;
    cv::Mat dominance_green;
    cv::Mat dominance_blue;
    cv::Mat competing_color;
    cv::Mat reflection_mask;
    cv::Mat local_background;
    cv::Mat local_peak;
    cv::Mat local_contrast_mask;
    cv::compare(channels[2], std::clamp(cfg.red_min, 0, 255), red_mask, cv::CMP_GE);
    cv::subtract(channels[2], channels[1], red_green);
    cv::subtract(channels[2], channels[0], red_blue);
    cv::compare(red_green, std::clamp(cfg.red_dominance, 0, 255),
                dominance_green, cv::CMP_GE);
    cv::compare(red_blue, std::clamp(cfg.red_dominance, 0, 255),
                dominance_blue, cv::CMP_GE);
    cv::max(channels[0], channels[1], competing_color);
    cv::compare(competing_color, std::clamp(cfg.reflection_max, 0, 255),
                reflection_mask, cv::CMP_LE);
    cv::bitwise_and(red_mask, dominance_green, red_mask);
    cv::bitwise_and(red_mask, dominance_blue, red_mask);
    cv::bitwise_and(red_mask, reflection_mask, red_mask);
    cv::GaussianBlur(dominance_image, local_background, cv::Size(11, 11), 0.0);
    cv::subtract(dominance_image, local_background, local_peak);
    cv::compare(local_peak, std::clamp(cfg.local_contrast, 0, 255),
                local_contrast_mask, cv::CMP_GE);
    cv::bitwise_and(red_mask, local_contrast_mask, red_mask);

    double peak_dominance = 0.0;
    cv::minMaxLoc(dominance_image, nullptr, &peak_dominance,
                  nullptr, &best.peak_point);
    best.peak_dominance = static_cast<float>(peak_dominance);
    best.peak_red = static_cast<int>(channels[2].at<uint8_t>(best.peak_point));

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(red_mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    const float frame_diagonal = std::sqrt(
        static_cast<float>(bgr.cols * bgr.cols + bgr.rows * bgr.rows));

    for (const auto &contour : contours) {
        const cv::Rect bounds = cv::boundingRect(contour);
        if (bounds.width <= 0 || bounds.height <= 0 ||
            bounds.width > std::max(1, cfg.max_span) ||
            bounds.height > std::max(1, cfg.max_span)) {
            continue;
        }
        const int area = cv::countNonZero(red_mask(bounds));
        if (area < std::max(1, cfg.min_area) ||
            area > std::max(cfg.min_area, cfg.max_area)) {
            continue;
        }
        const float aspect = static_cast<float>(std::max(bounds.width, bounds.height)) /
                             static_cast<float>(std::max(1, std::min(bounds.width, bounds.height)));
        const float fill = static_cast<float>(area) /
                           static_cast<float>(bounds.width * bounds.height);
        if (aspect > 3.5f || fill < 0.12f) {
            continue;
        }

        const cv::Moments moments = cv::moments(red_mask(bounds), true);
        if (moments.m00 <= 0.0) {
            continue;
        }
        const cv::Point2f center(
            bounds.x + static_cast<float>(moments.m10 / moments.m00),
            bounds.y + static_cast<float>(moments.m01 / moments.m00));
        const cv::Scalar mean_bgr = cv::mean(bgr(bounds), red_mask(bounds));
        const float dominance = static_cast<float>(
            mean_bgr[2] - std::max(mean_bgr[0], mean_bgr[1]));
        const float target_distance = frame_diagonal > 1.0f
            ? cv::norm(center - target_center) / frame_diagonal
            : 1.0f;
        const float score = static_cast<float>(mean_bgr[2]) + dominance +
                            fill * 40.0f - target_distance * 15.0f -
                            static_cast<float>(area) * 0.03f;
        if (!best.valid || score > best.score) {
            best.valid = true;
            best.center = center;
            best.bounds = bounds;
            best.area = area;
            best.score = score;
        }
    }
    return best;
}

RedLaserDetection detect_laser_motion(const cv::Mat &reference_bgr,
                                      const cv::Mat &current_bgr,
                                      const LaserMotionDetectorConfig &cfg,
                                      const cv::Point2f &target_center) {
    RedLaserDetection best;
    if (reference_bgr.empty() || current_bgr.empty() ||
        reference_bgr.type() != CV_8UC3 || current_bgr.type() != CV_8UC3 ||
        reference_bgr.size() != current_bgr.size()) {
        return best;
    }

    std::vector<cv::Mat> reference_channels;
    std::vector<cv::Mat> current_channels;
    cv::split(reference_bgr, reference_channels);
    cv::split(current_bgr, current_channels);

    cv::Mat delta_red;
    cv::Mat local_background;
    cv::Mat local_delta;
    cv::Mat delta_mask;
    cv::Mat local_mask;
    cv::subtract(current_channels[2], reference_channels[2], delta_red);
    cv::GaussianBlur(delta_red, local_background, cv::Size(11, 11), 0.0);
    cv::subtract(delta_red, local_background, local_delta);
    cv::compare(delta_red, std::clamp(cfg.delta_min, 0, 255),
                delta_mask, cv::CMP_GE);
    cv::compare(local_delta, std::clamp(cfg.local_min, 0, 255),
                local_mask, cv::CMP_GE);
    cv::bitwise_and(delta_mask, local_mask, delta_mask);

    double peak_delta = 0.0;
    cv::minMaxLoc(delta_red, nullptr, &peak_delta, nullptr, &best.peak_point);
    best.peak_dominance = static_cast<float>(peak_delta);
    best.peak_red = static_cast<int>(
        current_channels[2].at<uint8_t>(best.peak_point));

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(delta_mask, contours, cv::RETR_EXTERNAL,
                     cv::CHAIN_APPROX_SIMPLE);
    const float frame_diagonal = std::sqrt(
        static_cast<float>(current_bgr.cols * current_bgr.cols +
                           current_bgr.rows * current_bgr.rows));

    for (const auto &contour : contours) {
        const cv::Rect bounds = cv::boundingRect(contour);
        if (bounds.width <= 0 || bounds.height <= 0 ||
            bounds.width > std::max(1, cfg.max_span) ||
            bounds.height > std::max(1, cfg.max_span)) {
            continue;
        }
        const int area = cv::countNonZero(delta_mask(bounds));
        if (area < std::max(1, cfg.min_area) ||
            area > std::max(cfg.min_area, cfg.max_area)) {
            continue;
        }
        const float aspect = static_cast<float>(
            std::max(bounds.width, bounds.height)) /
            static_cast<float>(std::max(1, std::min(bounds.width, bounds.height)));
        const float fill = static_cast<float>(area) /
                           static_cast<float>(bounds.width * bounds.height);
        if (aspect > 3.5f || fill < 0.12f) {
            continue;
        }

        const cv::Moments moments = cv::moments(delta_mask(bounds), true);
        if (moments.m00 <= 0.0) {
            continue;
        }
        const cv::Point2f center(
            bounds.x + static_cast<float>(moments.m10 / moments.m00),
            bounds.y + static_cast<float>(moments.m01 / moments.m00));
        const cv::Scalar mean_delta = cv::mean(delta_red(bounds), delta_mask(bounds));
        double component_peak = 0.0;
        cv::minMaxLoc(delta_red(bounds), nullptr, &component_peak, nullptr, nullptr,
                      delta_mask(bounds));
        const float target_distance = frame_diagonal > 1.0f
            ? cv::norm(center - target_center) / frame_diagonal
            : 1.0f;
        const float score = static_cast<float>(mean_delta[0]) * 4.0f +
                            static_cast<float>(component_peak) + fill * 20.0f -
                            target_distance * 8.0f - static_cast<float>(area) * 0.02f;
        if (!best.valid || score > best.score) {
            best.valid = true;
            best.center = center;
            best.bounds = bounds;
            best.area = area;
            best.score = score;
        }
    }
    return best;
}

void draw_red_laser_debug_view(cv::Mat &frame, float gain) {
    const cv::Mat dominance = red_dominance_image(frame);
    if (dominance.empty()) {
        return;
    }
    cv::Mat enhanced;
    dominance.convertTo(enhanced, CV_8U, std::max(0.1f, gain));
    cv::Mat heatmap;
    cv::applyColorMap(enhanced, heatmap, cv::COLORMAP_HOT);
    const int preview_w = std::min(320, frame.cols / 3);
    const int preview_h = std::max(1, preview_w * frame.rows / frame.cols);
    cv::resize(heatmap, heatmap, cv::Size(preview_w, preview_h),
               0.0, 0.0, cv::INTER_AREA);
    const int x = std::max(0, frame.cols - preview_w - 18);
    const int y = 48;
    if (y + preview_h > frame.rows) {
        return;
    }
    heatmap.copyTo(frame(cv::Rect(x, y, preview_w, preview_h)));
    cv::rectangle(frame, cv::Rect(x, y, preview_w, preview_h),
                  cv::Scalar(255, 255, 255), 2, cv::LINE_AA);
    cv::putText(frame, "IR/RED ENHANCED", cv::Point(x + 8, y + 24),
                cv::FONT_HERSHEY_DUPLEX, 0.65,
                cv::Scalar(255, 255, 255), 2, cv::LINE_AA);
}

int main(int argc, char *argv[])
{

    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <config yaml>" << std::endl;
        return 1;
    }



    can_init();//can初始化



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

    const bool aim_follow_gimbal_enable = get_env_bool(AIM_FOLLOW_GIMBAL_ENABLE_ENV, true);
    const bool aim_follow_chassis_enable = get_env_bool(AIM_FOLLOW_CHASSIS_ENABLE_ENV, true);
    const bool aim_follow_laser_aim_enable =
        get_env_bool("AIM_FOLLOW_LASER_AIM_ENABLE", AIM_FOLLOW_LASER_AIM_ENABLE);
    const float aim_follow_laser_center_gate_norm =
        get_env_float("AIM_FOLLOW_LASER_CENTER_GATE_NORM",
                      AIM_FOLLOW_LASER_CENTER_GATE_NORM, 0.01f, 0.5f);
    const int aim_follow_laser_min_yaw =
        get_env_int("AIM_FOLLOW_LASER_MIN_YAW", 100, CMD_YAW_MIN, CMD_YAW_MAX);
    const int aim_follow_laser_max_yaw =
        get_env_int("AIM_FOLLOW_LASER_MAX_YAW", 165, CMD_YAW_MIN, CMD_YAW_MAX);
    const int aim_follow_laser_min_pitch =
        get_env_int("AIM_FOLLOW_LASER_MIN_PITCH", 120, CMD_PITCH_MIN, CMD_PITCH_MAX);
    const int aim_follow_laser_max_pitch =
        get_env_int("AIM_FOLLOW_LASER_MAX_PITCH", 180, CMD_PITCH_MIN, CMD_PITCH_MAX);
    const int aim_follow_laser_center_hold_frames =
        get_env_int("AIM_FOLLOW_LASER_CENTER_HOLD_FRAMES", 8, 1, 120);
    const int aim_follow_laser_confirm_frames =
        get_env_int("AIM_FOLLOW_LASER_CONFIRM_FRAMES", 2, 1, 30);
    const int aim_follow_laser_lost_frames =
        get_env_int("AIM_FOLLOW_LASER_LOST_FRAMES", 5, 0, 120);
    const int aim_follow_laser_coarse_hold_frames =
        get_env_int("AIM_FOLLOW_LASER_COARSE_HOLD_FRAMES", 5, 1, 120);
    const int aim_follow_laser_coarse_yaw_step =
        get_env_int("AIM_FOLLOW_LASER_COARSE_YAW_STEP", 5, 1, 20);
    const int aim_follow_laser_coarse_pitch_step =
        get_env_int("AIM_FOLLOW_LASER_COARSE_PITCH_STEP", 5, 1, 20);
    const bool aim_follow_laser_coarse_yaw_enable =
        get_env_bool("AIM_FOLLOW_LASER_COARSE_YAW_ENABLE",
                     AIM_FOLLOW_LASER_COARSE_YAW_ENABLE);
    const float aim_follow_laser_coarse_motion_px =
        get_env_float("AIM_FOLLOW_LASER_COARSE_MOTION_PX", 4.0f, 0.0f, 100.0f);
    const float aim_follow_laser_fine_deadzone_norm =
        get_env_float("AIM_FOLLOW_LASER_FINE_DEADZONE_NORM", 0.015f, 0.001f, 0.2f);
    const float aim_follow_laser_fine_yaw_kp =
        get_env_float("AIM_FOLLOW_LASER_FINE_YAW_KP", 6.0f, 0.1f, 50.0f);
    const float aim_follow_laser_fine_pitch_kp =
        get_env_float("AIM_FOLLOW_LASER_FINE_PITCH_KP", 6.0f, 0.1f, 50.0f);
    const int aim_follow_laser_fine_max_step =
        get_env_int("AIM_FOLLOW_LASER_FINE_MAX_STEP", 2, 1, 10);
    const bool aim_follow_laser_fine_yaw_enable =
        get_env_bool("AIM_FOLLOW_LASER_FINE_YAW_ENABLE",
                     AIM_FOLLOW_LASER_FINE_YAW_ENABLE);
    const int aim_follow_laser_lock_hold_frames =
        get_env_int("AIM_FOLLOW_LASER_LOCK_HOLD_FRAMES", 5, 1, 120);
    const bool aim_follow_laser_invert_yaw =
        get_env_bool("AIM_FOLLOW_LASER_INVERT_YAW", false);
    const bool aim_follow_laser_invert_pitch =
        get_env_bool("AIM_FOLLOW_LASER_INVERT_PITCH", false);
    const bool aim_follow_laser_debug_view =
        get_env_bool("AIM_FOLLOW_LASER_DEBUG_VIEW", AIM_FOLLOW_LASER_DEBUG_VIEW);
    const float aim_follow_laser_debug_gain =
        get_env_float("AIM_FOLLOW_LASER_DEBUG_GAIN",
                      AIM_FOLLOW_LASER_DEBUG_GAIN, 0.1f, 20.0f);
    RedLaserDetectorConfig red_laser_cfg;
    red_laser_cfg.red_min =
        get_env_int("AIM_FOLLOW_LASER_RED_MIN", AIM_FOLLOW_LASER_RED_MIN, 0, 255);
    red_laser_cfg.red_dominance =
        get_env_int("AIM_FOLLOW_LASER_RED_DOMINANCE",
                    AIM_FOLLOW_LASER_RED_DOMINANCE, 0, 255);
    red_laser_cfg.reflection_max =
        get_env_int("AIM_FOLLOW_LASER_REFLECTION_MAX",
                    AIM_FOLLOW_LASER_REFLECTION_MAX, 0, 255);
    red_laser_cfg.local_contrast =
        get_env_int("AIM_FOLLOW_LASER_LOCAL_CONTRAST",
                    AIM_FOLLOW_LASER_LOCAL_CONTRAST, 0, 255);
    red_laser_cfg.min_area =
        get_env_int("AIM_FOLLOW_LASER_MIN_AREA", AIM_FOLLOW_LASER_MIN_AREA, 1, 1000);
    red_laser_cfg.max_area =
        get_env_int("AIM_FOLLOW_LASER_MAX_AREA", AIM_FOLLOW_LASER_MAX_AREA, 1, 5000);
    red_laser_cfg.max_span =
        get_env_int("AIM_FOLLOW_LASER_MAX_SPAN", AIM_FOLLOW_LASER_MAX_SPAN, 1, 200);
    const bool aim_follow_laser_motion_enable =
        get_env_bool("AIM_FOLLOW_LASER_MOTION_ENABLE",
                     AIM_FOLLOW_LASER_MOTION_ENABLE);
    LaserMotionDetectorConfig laser_motion_cfg;
    laser_motion_cfg.delta_min =
        get_env_int("AIM_FOLLOW_LASER_MOTION_DELTA_MIN",
                    AIM_FOLLOW_LASER_MOTION_DELTA_MIN, 1, 255);
    laser_motion_cfg.local_min =
        get_env_int("AIM_FOLLOW_LASER_MOTION_LOCAL_MIN",
                    AIM_FOLLOW_LASER_MOTION_LOCAL_MIN, 1, 255);
    laser_motion_cfg.min_area =
        get_env_int("AIM_FOLLOW_LASER_MOTION_MIN_AREA",
                    AIM_FOLLOW_LASER_MOTION_MIN_AREA, 1, 1000);
    laser_motion_cfg.max_area =
        get_env_int("AIM_FOLLOW_LASER_MOTION_MAX_AREA",
                    AIM_FOLLOW_LASER_MOTION_MAX_AREA, 1, 5000);
    laser_motion_cfg.max_span =
        get_env_int("AIM_FOLLOW_LASER_MOTION_MAX_SPAN",
                    AIM_FOLLOW_LASER_MOTION_MAX_SPAN, 1, 200);
    const int aim_follow_laser_motion_settle_frames =
        get_env_int("AIM_FOLLOW_LASER_MOTION_SETTLE_FRAMES",
                    AIM_FOLLOW_LASER_MOTION_SETTLE_FRAMES, 0, 30);
    const int aim_follow_laser_motion_sample_frames =
        get_env_int("AIM_FOLLOW_LASER_MOTION_SAMPLE_FRAMES",
                    AIM_FOLLOW_LASER_MOTION_SAMPLE_FRAMES, 1, 30);
    const int aim_follow_laser_motion_hold_frames =
        get_env_int("AIM_FOLLOW_LASER_MOTION_HOLD_FRAMES",
                    AIM_FOLLOW_LASER_MOTION_HOLD_FRAMES, 1, 120);
    const int aim_follow_gimbal_aux =
        get_env_int("AIM_FOLLOW_GIMBAL_AUX", GIMBAL_AUX_CENTER, CMD_PITCH_MIN, CMD_PITCH_MAX);
    const int aim_follow_gimbal_repeat =
        get_env_int("AIM_FOLLOW_GIMBAL_REPEAT", GIMBAL_COMMAND_REPEAT, 1, 8);
    const float aim_follow_target_distance_m =
        get_env_float("AIM_FOLLOW_TARGET_DISTANCE_M", AIM_FOLLOW_TARGET_DISTANCE_M, 0.2f, 10.0f);
    const float aim_follow_yaw_kp =
        get_env_float("AIM_FOLLOW_YAW_KP", AIM_FOLLOW_YAW_KP, 0.0f, 200.0f);
    const float aim_follow_yaw_kd =
        get_env_float("AIM_FOLLOW_YAW_KD", AIM_FOLLOW_YAW_KD, 0.0f, 100.0f);
    const float aim_follow_pitch_kp =
        get_env_float("AIM_FOLLOW_PITCH_KP", AIM_FOLLOW_PITCH_KP, 0.0f, 200.0f);
    const float aim_follow_pitch_kd =
        get_env_float("AIM_FOLLOW_PITCH_KD", AIM_FOLLOW_PITCH_KD, 0.0f, 100.0f);
    const bool aim_follow_invert_yaw =
        get_env_bool("AIM_FOLLOW_INVERT_YAW", AIM_FOLLOW_INVERT_YAW);
    const bool aim_follow_invert_pitch =
        get_env_bool("AIM_FOLLOW_INVERT_PITCH", AIM_FOLLOW_INVERT_PITCH);
    const float aim_follow_aim_deadzone_norm =
        get_env_float("AIM_FOLLOW_AIM_DEADZONE_NORM", AIM_FOLLOW_AIM_DEADZONE_NORM, 0.0f, 0.5f);
    const float aim_follow_max_cmd_step =
        get_env_float("AIM_FOLLOW_MAX_CMD_STEP", AIM_FOLLOW_MAX_CMD_STEP, 1.0f, 50.0f);
    const float aim_follow_distance_deadband_m =
        get_env_float("AIM_FOLLOW_DISTANCE_DEADBAND_M", AIM_FOLLOW_DISTANCE_DEADBAND_M, 0.0f, 3.0f);
    const float aim_follow_distance_resume_deadband_m = std::max(
        aim_follow_distance_deadband_m,
        get_env_float("AIM_FOLLOW_DISTANCE_RESUME_DEADBAND_M",
                      AIM_FOLLOW_DISTANCE_RESUME_DEADBAND_M,
                      0.0f,
                      3.0f));
    const float aim_follow_follow_kp_rpm_per_m =
        get_env_float("AIM_FOLLOW_FOLLOW_KP_RPM_PER_M", AIM_FOLLOW_FOLLOW_KP_RPM_PER_M, 0.0f, 1000.0f);
    const int aim_follow_min_follow_rpm =
        get_env_int("AIM_FOLLOW_MIN_FOLLOW_RPM", AIM_FOLLOW_MIN_FOLLOW_RPM, 0, 1000);
    const int aim_follow_max_follow_rpm =
        get_env_int("AIM_FOLLOW_MAX_FOLLOW_RPM", AIM_FOLLOW_MAX_FOLLOW_RPM, 0, 1000);
    const int aim_follow_motor1_forward_sign =
        get_env_int("AIM_FOLLOW_MOTOR1_FORWARD_SIGN", AIM_FOLLOW_MOTOR1_FORWARD_SIGN, -1, 1) < 0 ? -1 : 1;
    const int aim_follow_motor2_forward_sign =
        get_env_int("AIM_FOLLOW_MOTOR2_FORWARD_SIGN", AIM_FOLLOW_MOTOR2_FORWARD_SIGN, -1, 1) < 0 ? -1 : 1;
    const bool aim_follow_distance_enable =
        get_env_bool("AIM_FOLLOW_DISTANCE_ENABLE", AIM_FOLLOW_DISTANCE_ENABLE);
    const bool aim_follow_chassis_steer_enable =
        get_env_bool("AIM_FOLLOW_CHASSIS_STEER_ENABLE", AIM_FOLLOW_CHASSIS_STEER_ENABLE);
    const float aim_follow_steer_deadzone_norm =
        get_env_float("AIM_FOLLOW_STEER_DEADZONE_NORM", AIM_FOLLOW_STEER_DEADZONE_NORM, 0.0f, 0.8f);
    const float aim_follow_steer_kp_rpm =
        get_env_float("AIM_FOLLOW_STEER_KP_RPM", AIM_FOLLOW_STEER_KP_RPM, 0.0f, 1000.0f);
    const int aim_follow_min_steer_rpm =
        get_env_int("AIM_FOLLOW_MIN_STEER_RPM", AIM_FOLLOW_MIN_STEER_RPM, 0, 1000);
    const int aim_follow_max_steer_rpm =
        get_env_int("AIM_FOLLOW_MAX_STEER_RPM", AIM_FOLLOW_MAX_STEER_RPM, 0, 1000);
    const int aim_follow_motor1_steer_sign =
        get_env_int("AIM_FOLLOW_MOTOR1_STEER_SIGN", AIM_FOLLOW_MOTOR1_STEER_SIGN, -1, 1) < 0 ? -1 : 1;
    const int aim_follow_motor2_steer_sign =
        get_env_int("AIM_FOLLOW_MOTOR2_STEER_SIGN", AIM_FOLLOW_MOTOR2_STEER_SIGN, -1, 1) < 0 ? -1 : 1;
    const bool aim_follow_search_enable =
        get_env_bool("AIM_FOLLOW_SEARCH_ENABLE", AIM_FOLLOW_SEARCH_ENABLE);
    const int aim_follow_search_rpm =
        get_env_int("AIM_FOLLOW_SEARCH_RPM", AIM_FOLLOW_SEARCH_RPM, 0, 40);
    const int aim_follow_search_sweep_frames =
        get_env_int("AIM_FOLLOW_SEARCH_SWEEP_FRAMES", AIM_FOLLOW_SEARCH_SWEEP_FRAMES, 10, 600);
    const int aim_follow_default_search_direction =
        get_env_int("AIM_FOLLOW_DEFAULT_SEARCH_DIRECTION", AIM_FOLLOW_DEFAULT_SEARCH_DIRECTION, -1, 1) < 0 ? -1 : 1;
    const float distance_target_real_width_m =
        get_env_float("AIM_FOLLOW_TARGET_REAL_WIDTH_M", DISTANCE_TARGET_REAL_WIDTH_M, 0.05f, 2.0f);
    const float distance_camera_focal_px =
        get_env_float("AIM_FOLLOW_DISTANCE_FOCAL_PX", DISTANCE_CAMERA_FOCAL_PX, 100.0f, 5000.0f);
    const float distance_filter_alpha =
        get_env_float("AIM_FOLLOW_DISTANCE_FILTER_ALPHA", DISTANCE_FILTER_ALPHA, 0.0f, 1.0f);
    const int distance_median_window_size =
        get_env_int("AIM_FOLLOW_DISTANCE_MEDIAN_WINDOW", DISTANCE_MEDIAN_WINDOW_SIZE, 1, 31);
    const float distance_stability_deadband_m =
        get_env_float("AIM_FOLLOW_DISTANCE_STABILITY_DEADBAND_M", DISTANCE_STABILITY_DEADBAND_M, 0.0f, 0.5f);
    const float distance_max_filtered_step_m =
        get_env_float("AIM_FOLLOW_DISTANCE_MAX_STEP_M", DISTANCE_MAX_FILTERED_STEP_M, 0.001f, 2.0f);
    const bool aim_follow_bytetrack_enable =
        get_env_bool("AIM_FOLLOW_BYTETRACK_ENABLE", AIM_FOLLOW_BYTETRACK_ENABLE);
    const int aim_follow_bytetrack_frame_rate =
        get_env_int("AIM_FOLLOW_BYTETRACK_FRAME_RATE", AIM_FOLLOW_BYTETRACK_FRAME_RATE, 1, 120);
    const int aim_follow_bytetrack_buffer_frames =
        get_env_int("AIM_FOLLOW_BYTETRACK_BUFFER_FRAMES", AIM_FOLLOW_BYTETRACK_BUFFER_FRAMES, 1, 300);
    const int aim_follow_bytetrack_switch_delay_frames =
        get_env_int("AIM_FOLLOW_BYTETRACK_SWITCH_DELAY_FRAMES",
                    AIM_FOLLOW_BYTETRACK_SWITCH_DELAY_FRAMES, 0, 30);
    const float aim_follow_bytetrack_track_thresh =
        get_env_float("AIM_FOLLOW_BYTETRACK_TRACK_THRESH",
                      AIM_FOLLOW_BYTETRACK_TRACK_THRESH, 0.1f, 0.95f);
    const float aim_follow_bytetrack_high_thresh =
        get_env_float("AIM_FOLLOW_BYTETRACK_HIGH_THRESH",
                      AIM_FOLLOW_BYTETRACK_HIGH_THRESH, 0.1f, 0.99f);
    const float aim_follow_bytetrack_match_thresh =
        get_env_float("AIM_FOLLOW_BYTETRACK_MATCH_THRESH",
                      AIM_FOLLOW_BYTETRACK_MATCH_THRESH, 0.1f, 1.0f);

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

    aim_follow::TargetSelectorConfig selector_cfg;
    selector_cfg.frame_width = static_cast<float>(FRAME_W);
    selector_cfg.frame_height = static_cast<float>(FRAME_H);
    selector_cfg.max_center_jump_norm = AIM_FOLLOW_SELECTOR_MAX_CENTER_JUMP_NORM;
    selector_cfg.area_switch_ratio = AIM_FOLLOW_SELECTOR_AREA_SWITCH_RATIO;
    selector_cfg.max_lost_frames = AIM_FOLLOW_SELECTOR_MAX_LOST_FRAMES;
    aim_follow::TargetSelector target_selector(selector_cfg);

    BYTETracker bicycle_tracker(aim_follow_bytetrack_frame_rate,
                                aim_follow_bytetrack_buffer_frames,
                                aim_follow_bytetrack_track_thresh,
                                aim_follow_bytetrack_high_thresh,
                                aim_follow_bytetrack_match_thresh);
    aim_follow::TrackedTargetSelectorConfig tracked_selector_cfg;
    tracked_selector_cfg.max_missing_frames = aim_follow_bytetrack_switch_delay_frames;
    aim_follow::TrackedTargetSelector tracked_target_selector(tracked_selector_cfg);
    aim_follow::StableTrackIdConfig stable_id_cfg;
    stable_id_cfg.frame_width = static_cast<float>(yolov5_cfg.FRAME_W);
    stable_id_cfg.frame_height = static_cast<float>(yolov5_cfg.FRAME_H);
    stable_id_cfg.max_missing_frames = aim_follow_bytetrack_buffer_frames;
    aim_follow::StableTrackIdMapper stable_id_mapper(stable_id_cfg);

    aim_follow::DistanceEstimatorConfig distance_cfg;
    distance_cfg.target_real_width_m = distance_target_real_width_m;
    distance_cfg.focal_length_px = distance_camera_focal_px;
    distance_cfg.min_box_width_px = DISTANCE_MIN_BOX_WIDTH_PX;
    distance_cfg.filter_alpha = distance_filter_alpha;
    distance_cfg.median_window_size = distance_median_window_size;
    distance_cfg.stability_deadband_m = distance_stability_deadband_m;
    distance_cfg.max_filtered_step_m = distance_max_filtered_step_m;
    aim_follow::MonocularDistanceEstimator distance_estimator(distance_cfg);

    aim_follow::ControlConfig control_cfg;
    control_cfg.frame_width = static_cast<float>(FRAME_W);
    control_cfg.frame_height = static_cast<float>(FRAME_H);
    control_cfg.center_yaw = CENTER_YAW;
    control_cfg.center_pitch = CENTER_PITCH;
    control_cfg.min_yaw = CMD_YAW_MIN;
    control_cfg.max_yaw = CMD_YAW_MAX;
    control_cfg.min_pitch = CMD_PITCH_MIN;
    control_cfg.max_pitch = CMD_PITCH_MAX;
    control_cfg.yaw_kp = aim_follow_yaw_kp;
    control_cfg.yaw_kd = aim_follow_yaw_kd;
    control_cfg.pitch_kp = aim_follow_pitch_kp;
    control_cfg.pitch_kd = aim_follow_pitch_kd;
    control_cfg.aim_deadzone_norm = aim_follow_aim_deadzone_norm;
    control_cfg.max_cmd_step = aim_follow_max_cmd_step;
    control_cfg.invert_yaw = aim_follow_invert_yaw;
    control_cfg.invert_pitch = aim_follow_invert_pitch;
    control_cfg.target_distance_m = aim_follow_target_distance_m;
    control_cfg.distance_deadband_m = aim_follow_distance_deadband_m;
    control_cfg.distance_resume_deadband_m = aim_follow_distance_resume_deadband_m;
    control_cfg.follow_kp_rpm_per_m = aim_follow_follow_kp_rpm_per_m;
    control_cfg.min_follow_rpm = aim_follow_min_follow_rpm;
    control_cfg.max_follow_rpm = aim_follow_max_follow_rpm;
    control_cfg.motor1_forward_sign = aim_follow_motor1_forward_sign;
    control_cfg.motor2_forward_sign = aim_follow_motor2_forward_sign;
    control_cfg.distance_follow_enabled = aim_follow_distance_enable;
    control_cfg.chassis_steer_enabled = aim_follow_chassis_steer_enable;
    control_cfg.steer_deadzone_norm = aim_follow_steer_deadzone_norm;
    control_cfg.steer_kp_rpm = aim_follow_steer_kp_rpm;
    control_cfg.min_steer_rpm = aim_follow_min_steer_rpm;
    control_cfg.max_steer_rpm = aim_follow_max_steer_rpm;
    control_cfg.motor1_steer_sign = aim_follow_motor1_steer_sign;
    control_cfg.motor2_steer_sign = aim_follow_motor2_steer_sign;
    control_cfg.lost_hold_frames = AIM_FOLLOW_LOST_HOLD_FRAMES;
    control_cfg.search_enabled = aim_follow_search_enable;
    control_cfg.search_rpm = aim_follow_search_rpm;
    control_cfg.search_sweep_frames = aim_follow_search_sweep_frames;
    control_cfg.default_search_direction = aim_follow_default_search_direction;
    aim_follow::AimFollowController follow_controller(control_cfg);

    aim_follow::LaserAimConfig laser_aim_cfg;
    laser_aim_cfg.frame_width = static_cast<float>(FRAME_W);
    laser_aim_cfg.frame_height = static_cast<float>(FRAME_H);
    laser_aim_cfg.center_yaw = CENTER_YAW;
    laser_aim_cfg.center_pitch = CENTER_PITCH;
    laser_aim_cfg.min_yaw = std::min(aim_follow_laser_min_yaw, aim_follow_laser_max_yaw);
    laser_aim_cfg.max_yaw = std::max(aim_follow_laser_min_yaw, aim_follow_laser_max_yaw);
    laser_aim_cfg.min_pitch = std::min(aim_follow_laser_min_pitch, aim_follow_laser_max_pitch);
    laser_aim_cfg.max_pitch = std::max(aim_follow_laser_min_pitch, aim_follow_laser_max_pitch);
    laser_aim_cfg.centered_hold_frames = aim_follow_laser_center_hold_frames;
    laser_aim_cfg.laser_confirm_frames = aim_follow_laser_confirm_frames;
    laser_aim_cfg.laser_lost_frames = aim_follow_laser_lost_frames;
    laser_aim_cfg.coarse_hold_frames = aim_follow_laser_coarse_hold_frames;
    laser_aim_cfg.coarse_yaw_step = aim_follow_laser_coarse_yaw_step;
    laser_aim_cfg.coarse_pitch_step = aim_follow_laser_coarse_pitch_step;
    laser_aim_cfg.coarse_yaw_enable = aim_follow_laser_coarse_yaw_enable;
    laser_aim_cfg.coarse_laser_motion_min_px = aim_follow_laser_coarse_motion_px;
    laser_aim_cfg.fine_deadzone_norm = aim_follow_laser_fine_deadzone_norm;
    laser_aim_cfg.fine_yaw_kp = aim_follow_laser_fine_yaw_kp;
    laser_aim_cfg.fine_pitch_kp = aim_follow_laser_fine_pitch_kp;
    laser_aim_cfg.fine_max_step = aim_follow_laser_fine_max_step;
    laser_aim_cfg.fine_yaw_enable = aim_follow_laser_fine_yaw_enable;
    laser_aim_cfg.lock_hold_frames = aim_follow_laser_lock_hold_frames;
    laser_aim_cfg.invert_yaw = aim_follow_laser_invert_yaw;
    laser_aim_cfg.invert_pitch = aim_follow_laser_invert_pitch;
    aim_follow::LaserAimController laser_aim_controller(laser_aim_cfg);
    cv::Mat laser_motion_reference;
    RedLaserDetection held_laser_motion_detection;
    int laser_motion_settle_remaining = 0;
    int laser_motion_sample_remaining = 0;
    int laser_motion_hold_remaining = 0;

    int bicycle_class_id = -1;
    for (int label_idx = 0; label_idx < static_cast<int>(yolov5_cfg.LABELS.size()); ++label_idx) {
        if (yolov5_cfg.LABELS[label_idx] == "bicycle") {
            bicycle_class_id = label_idx;
            break;
        }
    }

    std::cout << "[AIM FOLLOW CONFIG] startup target_distance_m=" << aim_follow_target_distance_m
              << " yaw_kp=" << aim_follow_yaw_kp
              << " pitch_kp=" << aim_follow_pitch_kp
              << " distance_width_m=" << distance_target_real_width_m
              << " distance_focal_px=" << distance_camera_focal_px
              << " distance_space=model"
              << " filter_alpha=" << distance_filter_alpha
              << " median_window=" << distance_median_window_size
              << " distance_hold_m=" << distance_stability_deadband_m
              << " distance_stop_deadband_m=" << aim_follow_distance_deadband_m
              << " distance_resume_deadband_m=" << aim_follow_distance_resume_deadband_m
              << " can_dryrun=" << (is_can_dry_run_enabled() ? 1 : 0)
              << " gimbal_enable=" << (aim_follow_gimbal_enable ? 1 : 0)
              << " chassis_enable=" << (aim_follow_chassis_enable ? 1 : 0)
              << " distance_enable=" << (aim_follow_distance_enable ? 1 : 0)
              << " steer_enable=" << (aim_follow_chassis_steer_enable ? 1 : 0)
              << " steer_max_rpm=" << aim_follow_max_steer_rpm
              << " steer_signs=" << aim_follow_motor1_steer_sign << "," << aim_follow_motor2_steer_sign
              << " search_enable=" << (aim_follow_search_enable ? 1 : 0)
              << " search_rpm=" << aim_follow_search_rpm
              << " search_sweep_frames=" << aim_follow_search_sweep_frames
              << " gimbal_aux=" << aim_follow_gimbal_aux
              << " gimbal_repeat=" << aim_follow_gimbal_repeat
              << " synthetic_target=" << (is_synthetic_target_enabled() ? 1 : 0)
              << " bytetrack=" << (aim_follow_bytetrack_enable ? 1 : 0)
              << " bytetrack_thresh=" << aim_follow_bytetrack_track_thresh
              << "/" << aim_follow_bytetrack_high_thresh
              << " laser_aim=" << (aim_follow_laser_aim_enable ? 1 : 0)
              << " laser_motion=" << (aim_follow_laser_motion_enable ? 1 : 0)
              << " laser_motion_delta=" << laser_motion_cfg.delta_min
              << "/" << laser_motion_cfg.local_min
              << " laser_yaw_axes="
              << (laser_aim_cfg.coarse_yaw_enable ? 1 : 0) << "/"
              << (laser_aim_cfg.fine_yaw_enable ? 1 : 0)
              << " laser_gate=" << aim_follow_laser_center_gate_norm
              << " laser_yaw_range=" << laser_aim_cfg.min_yaw << ":" << laser_aim_cfg.max_yaw
              << " laser_pitch_range=" << laser_aim_cfg.min_pitch << ":" << laser_aim_cfg.max_pitch
              << std::endl;
    std::vector<FPSCalculator> fps_calculators(CAMERA_COUNT); // 为每个流创建一个FPS计算器

    // 7. 配置单路流水线的输入和输出
    int i = 0;
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
        // 捕获所有“实现”需要的额外变量
        // 注意：这里的参数列表必须与 PostProcessingFunc 定义完全匹配
        // 使用[&]可以简化捕获列表
        [&](const IcoreMessageForPost &post_msg, cv::Mat &cvmat_to_draw)
    {
        if (post_msg.icore_tensors.empty())
        {
            LOG_WARN("[PostProcessor]", "Icore tensors are empty, cannot perform post-processing.");
            return;
        }
        // 从消息中获取source_id，这是查找正确上下文的关键
        int source_id = post_msg.meta.source_id;

        // 检查source_id的有效性，防止越界
        if (source_id < 0 || source_id >= 4)
        {
            LOG_ERROR("[PostProcessor]", "Invalid source_id {} received.", source_id);
            return;
        }
        cv::Mat laser_motion_current;
        if (aim_follow_laser_aim_enable && aim_follow_laser_motion_enable) {
            laser_motion_current = cvmat_to_draw.clone();
        }
        // 调用真正的实现，并传入所有捕获的参数
        auto &netinfo = netinfos[source_id];
        auto &last_results = last_results_group[source_id];
        auto &fps_calc = fps_calculators[source_id];

        fps_calc.tick();
        // LOG_DEBUG("[AI POST]", "[{}] Before post_detpost_plin, last_results size: {}", source_id, std::get<0>(last_results).size());
        YoloPostResult post_results;
        if (netinfo.DetPost_on)
        {
            post_results = post_detpost_plin(
                post_msg.icore_tensors, last_results, netinfo,
                yolov5_cfg.CONF, yolov5_cfg.IOU_THRESHOLD, yolov5_cfg.MULTILABEL, yolov5_cfg.FPGA_NMS, yolov5_cfg.N_CLASS, yolov5_cfg.ANCHORS,
                device);
        }
        else
        {
            post_results = post_detpost_soft(
                post_msg.icore_tensors, last_results, yolov5_cfg.LABELS, yolov5_cfg.ANCHORS, netinfo,
                yolov5_cfg.N_CLASS, yolov5_cfg.CONF, yolov5_cfg.IOU_THRESHOLD);
        }
        // Update last_results after the call
        last_results = post_results;

        std::vector<int> id_list = std::get<0>(post_results);
        std::vector<float> score_list = std::get<1>(post_results);
        std::vector<cv::Rect2f> box_list = std::get<2>(post_results);
        // LOG_DEBUG("[AI POST]", "[{}] After post_detpost_plin, last_results size: {}", source_id, std::get<0>(last_results).size());

        // ===================== 新增：类别过滤逻辑（仅保留自行车） =====================
        std::vector<int> filtered_id_list;
        std::vector<float> filtered_score_list;
        std::vector<cv::Rect2f> filtered_box_list;

        for (size_t i = 0; i < id_list.size(); ++i) {
            // 检查当前识别到的物体类别名称是否为 "bicycle"
            // 注意：这里的 "bicycle" 必须与你 names 文件中的名称完全一致！
            if (id_list[i] >= 0 &&
                id_list[i] < static_cast<int>(yolov5_cfg.LABELS.size()) &&
                yolov5_cfg.LABELS[id_list[i]] == "bicycle") {
                filtered_id_list.push_back(id_list[i]);
                filtered_score_list.push_back(score_list[i]);
                filtered_box_list.push_back(box_list[i]);
            }
        }

        // 用过滤后的列表替换原列表，这样后续的云台控制和画面画框就只会处理自行车了
        id_list = filtered_id_list;
        score_list = filtered_score_list;
        box_list = filtered_box_list;
        // =========================================================================


        std::tuple<bool, float, float, int, int> ratio_bias = camera_actor->getRatioBias();
        bool is_hw_resize = std::get<0>(ratio_bias);
        float RATIO_W = std::get<1>(ratio_bias);
        float RATIO_H = std::get<2>(ratio_bias);
        int BIAS_W = std::get<3>(ratio_bias);
        int BIAS_H = std::get<4>(ratio_bias);

        int target_index = -1;

        // 选最大目标
        float max_area = -1.0f;
        for (int i = 0; i < box_list.size(); ++i) {
            float area = box_list[i].width * box_list[i].height;
            if (area > max_area) {
                max_area = area;
                target_index = i;
            }
        }

        auto map_box_to_display = [&](const cv::Rect2f &box) -> cv::Rect2f {
            if (is_hw_resize) {
                return cv::Rect2f(
                    box.tl().x * RATIO_W + BIAS_W,
                    box.tl().y * RATIO_H + BIAS_H,
                    box.width * RATIO_W,
                    box.height * RATIO_H);
            }

            return cv::Rect2f(
                (box.tl().x - BIAS_W) / RATIO_W,
                (box.tl().y - BIAS_H) / RATIO_H,
                box.width / RATIO_W,
                box.height / RATIO_H);
        };

        auto display_box_to_model = [&](const cv::Rect2f &display_box) -> cv::Rect2f {
            if (is_hw_resize) {
                return cv::Rect2f(
                    (display_box.tl().x - BIAS_W) / RATIO_W,
                    (display_box.tl().y - BIAS_H) / RATIO_H,
                    display_box.width / RATIO_W,
                    display_box.height / RATIO_H);
            }

            return cv::Rect2f(
                display_box.tl().x * RATIO_W + BIAS_W,
                display_box.tl().y * RATIO_H + BIAS_H,
                display_box.width * RATIO_W,
                display_box.height * RATIO_H);
        };

        std::vector<cv::Rect2f> display_box_list;
        display_box_list.reserve(box_list.size());
        for (const auto &box : box_list) {
            display_box_list.push_back(map_box_to_display(box));
        }

        if (is_synthetic_target_enabled() && bicycle_class_id >= 0) {
            static int synthetic_frame = 0;
            ++synthetic_frame;
            const int phase = (synthetic_frame / 45) % 4;
            cv::Rect2f display_box;
            if (phase == 0) {
                display_box = cv::Rect2f(FRAME_W * 0.42f, FRAME_H * 0.40f, 190.0f, 150.0f);
            } else if (phase == 1) {
                display_box = cv::Rect2f(FRAME_W * 0.62f, FRAME_H * 0.42f, 210.0f, 160.0f);
            } else if (phase == 2) {
                display_box = cv::Rect2f(FRAME_W * 0.45f, FRAME_H * 0.22f, 220.0f, 170.0f);
            } else {
                display_box = cv::Rect2f(FRAME_W * 0.39f, FRAME_H * 0.36f, 340.0f, 260.0f);
            }

            id_list = {bicycle_class_id};
            score_list = {0.99f};
            box_list = {display_box_to_model(display_box)};
            display_box_list = {display_box};
            std::cout << "[SYNTHETIC TARGET] phase=" << phase
                      << " box=" << display_box.x << "," << display_box.y
                      << "," << display_box.width << "," << display_box.height
                      << std::endl;
        }

        bool has_control_target = false;
        int selected_track_id = -1;
        cv::Rect2f control_model_box;
        cv::Rect2f control_display_box;
        std::vector<std::pair<int, cv::Rect2f>> tracked_display_boxes;

        if (aim_follow_bytetrack_enable) {
            std::vector<byteTracker::Object> tracker_objects;
            tracker_objects.reserve(box_list.size());
            for (int det_idx = 0; det_idx < static_cast<int>(box_list.size()); ++det_idx) {
                byteTracker::Object object;
                object.rect = box_list[det_idx];
                object.label = bicycle_class_id;
                object.prob = score_list[det_idx];
                tracker_objects.push_back(object);
            }

            const auto tracks = bicycle_tracker.update(tracker_objects);
            tracked_display_boxes.reserve(tracks.size());
            std::vector<aim_follow::TrackedTargetCandidate> tracked_candidates;
            tracked_candidates.reserve(tracks.size());
            for (int track_idx = 0; track_idx < static_cast<int>(tracks.size()); ++track_idx) {
                const auto &track = tracks[track_idx];
                if (track.tlwh.size() < 4 || track.tlwh[2] <= 1.0f || track.tlwh[3] <= 1.0f) {
                    continue;
                }
                aim_follow::TrackedTargetCandidate candidate;
                candidate.index = track_idx;
                candidate.track_id = track.track_id;
                candidate.center_x = track.tlwh[0] + track.tlwh[2] * 0.5f;
                candidate.center_y = track.tlwh[1] + track.tlwh[3] * 0.5f;
                candidate.area = track.tlwh[2] * track.tlwh[3];
                candidate.score = track.score;
                tracked_candidates.push_back(candidate);
            }

            const auto stable_track_ids = stable_id_mapper.update(tracked_candidates);
            for (int candidate_pos = 0;
                 candidate_pos < static_cast<int>(tracked_candidates.size());
                 ++candidate_pos) {
                auto &candidate = tracked_candidates[candidate_pos];
                candidate.track_id = stable_track_ids[candidate_pos];
                const auto &track = tracks[candidate.index];
                tracked_display_boxes.emplace_back(
                    candidate.track_id,
                    map_box_to_display(cv::Rect2f(
                        track.tlwh[0], track.tlwh[1], track.tlwh[2], track.tlwh[3])));
            }

            const int selected_track_index = tracked_target_selector.select(tracked_candidates);
            if (selected_track_index >= 0 &&
                selected_track_index < static_cast<int>(tracks.size())) {
                const auto &track = tracks[selected_track_index];
                control_model_box = cv::Rect2f(
                    track.tlwh[0], track.tlwh[1], track.tlwh[2], track.tlwh[3]);
                control_display_box = map_box_to_display(control_model_box);
                selected_track_id = tracked_target_selector.lockedTrackId();
                has_control_target = true;

                float nearest_center_distance = std::numeric_limits<float>::max();
                const float track_cx = control_model_box.x + control_model_box.width * 0.5f;
                const float track_cy = control_model_box.y + control_model_box.height * 0.5f;
                for (int det_idx = 0; det_idx < static_cast<int>(box_list.size()); ++det_idx) {
                    const float det_cx = box_list[det_idx].x + box_list[det_idx].width * 0.5f;
                    const float det_cy = box_list[det_idx].y + box_list[det_idx].height * 0.5f;
                    const float dx = det_cx - track_cx;
                    const float dy = det_cy - track_cy;
                    const float distance = dx * dx + dy * dy;
                    if (distance < nearest_center_distance) {
                        nearest_center_distance = distance;
                        target_index = det_idx;
                    }
                }
            }

            static int bytetrack_log_counter = 0;
            static int last_logged_locked_id = -2;
            static int last_logged_selected_id = -2;
            static int last_logged_missing = -1;
            ++bytetrack_log_counter;
            const int locked_id = tracked_target_selector.lockedTrackId();
            const int missing_frames = tracked_target_selector.missingFrames();
            const bool track_state_changed =
                locked_id != last_logged_locked_id ||
                selected_track_id != last_logged_selected_id ||
                missing_frames != last_logged_missing;
            if (track_state_changed || bytetrack_log_counter % 30 == 0) {
                std::cout << "[BYTETRACK] detections=" << tracker_objects.size()
                          << " active_tracks=" << tracks.size()
                          << " locked_id=" << locked_id
                          << " selected_id=" << selected_track_id
                          << " missing=" << missing_frames
                          << std::endl;
                last_logged_locked_id = locked_id;
                last_logged_selected_id = selected_track_id;
                last_logged_missing = missing_frames;
            }
        } else {
            std::vector<aim_follow::TargetCandidate> target_candidates;
            for (int cand_idx = 0; cand_idx < static_cast<int>(display_box_list.size()); ++cand_idx) {
                const auto &box = display_box_list[cand_idx];
                aim_follow::TargetCandidate candidate;
                candidate.index = cand_idx;
                candidate.center_x = box.x + box.width * 0.5f;
                candidate.center_y = box.y + box.height * 0.5f;
                candidate.area = box.width * box.height;
                candidate.score = score_list[cand_idx];
                target_candidates.push_back(candidate);
            }
            target_index = target_selector.select(target_candidates);
            if (target_index >= 0 && target_index < static_cast<int>(box_list.size())) {
                control_model_box = box_list[target_index];
                control_display_box = display_box_list[target_index];
                has_control_target = true;
            }
        }

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
        RedLaserDetection red_laser_detection;
        bool laser_motion_confirmed = false;
        aim_follow::LaserAimOutput laser_aim_cmd;
        laser_aim_cmd.state = laser_aim_controller.state();
        laser_aim_cmd.pitch = last_pitch;
        laser_aim_cmd.yaw = last_yaw;
        bool gimbal_command_active = false;

        if (AIM_FOLLOW_CONTROL_ENABLE && has_control_target) {
            const auto &target_box = control_display_box;
            const float cx = target_box.x + target_box.width * 0.5f;
            const float cy = target_box.y + target_box.height * 0.5f;
            // The focal calibration is based on the detector's 640-wide model
            // space. Using the 1920-wide HDMI box here makes every distance
            // roughly three times too small.
            const float target_model_box_width = control_model_box.width;
            const auto distance = distance_estimator.update(target_model_box_width);
            target_raw_distance_m = distance.raw_distance_m;
            target_filtered_distance_m = distance.filtered_distance_m;

            aim_follow::TargetObservation obs;
            obs.valid = true;
            obs.center_x = cx;
            obs.center_y = cy;
            obs.box_width = target_model_box_width;
            obs.distance_m = distance.filtered_distance_m;
            obs.timestamp_s = std::chrono::duration<float>(
                std::chrono::steady_clock::now().time_since_epoch()).count();

            const auto follow_cmd = follow_controller.update(obs);
            control_target_valid = true;
            control_distance_error_m = follow_cmd.distance_error_m;
            control_ex = follow_cmd.norm_error_x;
            control_ey = follow_cmd.norm_error_y;
            control_motor1 = follow_cmd.motor1_rpm;
            control_motor2 = follow_cmd.motor2_rpm;
            control_pitch = follow_cmd.pitch;
            control_yaw = follow_cmd.yaw;
            if (!aim_follow_chassis_enable) {
                control_motor1 = 0;
                control_motor2 = 0;
            }

            if (aim_follow_laser_aim_enable) {
                aim_follow::LaserAimObservation laser_obs;
                laser_obs.target_valid = true;
                laser_obs.target_centered =
                    std::fabs(control_ex) <= aim_follow_laser_center_gate_norm;
                laser_obs.vehicle_stationary =
                    control_motor1 == 0 && control_motor2 == 0;
                const bool laser_gate_ready =
                    laser_obs.target_centered && laser_obs.vehicle_stationary;
                if (!laser_gate_ready) {
                    laser_motion_reference.release();
                    laser_motion_settle_remaining = 0;
                    laser_motion_sample_remaining = 0;
                    laser_motion_hold_remaining = 0;
                } else if (aim_follow_laser_motion_enable) {
                    if (laser_motion_sample_remaining > 0 &&
                        !laser_motion_reference.empty()) {
                        if (laser_motion_settle_remaining > 0) {
                            --laser_motion_settle_remaining;
                        } else {
                            red_laser_detection = detect_laser_motion(
                                laser_motion_reference,
                                laser_motion_current,
                                laser_motion_cfg,
                                cv::Point2f(cx, cy));
                            --laser_motion_sample_remaining;
                            if (red_laser_detection.valid) {
                                laser_motion_confirmed = true;
                                held_laser_motion_detection = red_laser_detection;
                                laser_motion_hold_remaining =
                                    aim_follow_laser_motion_hold_frames;
                                laser_motion_sample_remaining = 0;
                            }
                        }
                    }
                    if (!red_laser_detection.valid &&
                        laser_motion_hold_remaining > 0 &&
                        held_laser_motion_detection.valid) {
                        red_laser_detection = held_laser_motion_detection;
                        --laser_motion_hold_remaining;
                    }
                } else {
                    red_laser_detection = detect_red_laser(
                        cvmat_to_draw, red_laser_cfg, cv::Point2f(cx, cy));
                }
                laser_obs.target_center_x = cx;
                laser_obs.target_center_y = cy;
                laser_obs.laser_valid = red_laser_detection.valid;
                laser_obs.laser_motion_confirmed = laser_motion_confirmed;
                laser_obs.laser_x = red_laser_detection.center.x;
                laser_obs.laser_y = red_laser_detection.center.y;
                laser_aim_cmd = laser_aim_controller.update(laser_obs);
                control_pitch = laser_aim_cmd.pitch;
                control_yaw = laser_aim_cmd.yaw;
                gimbal_command_active = laser_aim_cmd.active;
                const bool gimbal_setpoint_changed =
                    gimbal_command_active &&
                    (control_pitch != last_pitch || control_yaw != last_yaw);
                if (aim_follow_laser_motion_enable &&
                    aim_follow_gimbal_enable && gimbal_setpoint_changed &&
                    !laser_motion_current.empty()) {
                    laser_motion_reference = laser_motion_current.clone();
                    laser_motion_settle_remaining =
                        aim_follow_laser_motion_settle_frames;
                    laser_motion_sample_remaining =
                        aim_follow_laser_motion_sample_frames;
                    laser_motion_hold_remaining = 0;
                    held_laser_motion_detection = RedLaserDetection();
                }
            } else {
                gimbal_command_active = true;
            }
            last_motor1 = control_motor1;
            last_motor2 = control_motor2;
            last_pitch = control_pitch;
            last_yaw = control_yaw;

            if (aim_follow_chassis_enable) {
                send_chassis_can_mode(control_motor1,
                                      control_motor2,
                                      CHASSIS_GIMBAL_NEUTRAL,
                                      CHASSIS_GIMBAL_NEUTRAL,
                                      TRIGGER_STOP,
                                      CAN_CONTROL_ENABLE);
            }
            if (aim_follow_gimbal_enable && gimbal_command_active) {
                send_gimbal_tracking_command(aim_follow_chassis_enable,
                                             control_pitch,
                                             control_yaw,
                                             aim_follow_gimbal_aux,
                                             aim_follow_gimbal_repeat);
            }

            std::cout << "[DISTANCE DEBUG] raw=" << target_raw_distance_m
                      << " filtered=" << target_filtered_distance_m
                      << " model_box_width=" << target_model_box_width
                      << " hdmi_box_width=" << target_box.width << std::endl;
            std::cout << "[AIM FOLLOW] cx=" << cx
                      << " cy=" << cy
                      << " motor1=" << control_motor1
                      << " motor2=" << control_motor2
                      << " forward=" << follow_cmd.forward_rpm
                      << " steer=" << follow_cmd.steer_rpm
                      << " pitch=" << control_pitch
                      << " yaw=" << control_yaw
                      << " ex=" << control_ex
                      << " ey=" << control_ey
                      << std::endl;
            if (aim_follow_laser_aim_enable) {
                std::cout << "[LASER AIM] state="
                          << aim_follow::laserAimStateName(laser_aim_cmd.state)
                          << " red=" << (red_laser_detection.valid ? 1 : 0)
                          << " motion=" << (laser_motion_confirmed ? 1 : 0)
                          << " red_xy=" << red_laser_detection.center.x
                          << "," << red_laser_detection.center.y
                          << " error=" << laser_aim_cmd.error_x
                          << "," << laser_aim_cmd.error_y
                          << " pitch=" << control_pitch
                          << " yaw=" << control_yaw
                          << std::endl;
            }
        } else if (AIM_FOLLOW_CONTROL_ENABLE) {
            aim_follow::TargetObservation obs;
            obs.valid = false;
            obs.timestamp_s = std::chrono::duration<float>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            const auto lost_cmd = follow_controller.update(obs);
            control_motor1 = lost_cmd.motor1_rpm;
            control_motor2 = lost_cmd.motor2_rpm;
            control_pitch = lost_cmd.pitch;
            control_yaw = lost_cmd.yaw;
            if (!aim_follow_chassis_enable) {
                control_motor1 = 0;
                control_motor2 = 0;
            }
            if (aim_follow_laser_aim_enable) {
                aim_follow::LaserAimObservation laser_obs;
                laser_aim_cmd = laser_aim_controller.update(laser_obs);
                control_pitch = laser_aim_cmd.pitch;
                control_yaw = laser_aim_cmd.yaw;
            }
            last_motor1 = control_motor1;
            last_motor2 = control_motor2;
            last_pitch = control_pitch;
            last_yaw = control_yaw;
            if (aim_follow_chassis_enable) {
                send_chassis_can_mode(control_motor1,
                                      control_motor2,
                                      CHASSIS_GIMBAL_NEUTRAL,
                                      CHASSIS_GIMBAL_NEUTRAL,
                                      TRIGGER_STOP,
                                      CAN_CONTROL_ENABLE);
            }
            if (aim_follow_gimbal_enable && !aim_follow_laser_aim_enable) {
                send_gimbal_tracking_command(aim_follow_chassis_enable,
                                             control_pitch,
                                             control_yaw,
                                             aim_follow_gimbal_aux,
                                             aim_follow_gimbal_repeat);
            }
            std::cout << "[AIM SEARCH] searching=" << (lost_cmd.searching ? 1 : 0)
                      << " motor1=" << control_motor1
                      << " motor2=" << control_motor2
                      << " steer=" << lost_cmd.steer_rpm
                      << std::endl;
        }

        if (aim_follow_laser_debug_view) {
            draw_red_laser_debug_view(cvmat_to_draw, aim_follow_laser_debug_gain);
        }

        const int panel_x = 18;
        const int panel_y = 56;
        const int panel_w = 900;
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
        const std::string track_text = selected_track_id >= 0
            ? std::to_string(selected_track_id)
            : "--";
        draw_control_text(fmt::format("Target:{} ID:{} Distance:{} Error:{:+.2f}m",
                                      target_state, track_text, distance_text, control_distance_error_m),
                          0, control_target_valid ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 255, 255));
        const std::string laser_state = aim_follow_laser_aim_enable
            ? aim_follow::laserAimStateName(laser_aim_cmd.state)
            : "OFF";
        draw_control_text(fmt::format("Laser:{} red:{} peak={:.0f}/{} pitch={} yaw={} err={:+.2f},{:+.2f}",
                                      laser_state,
                                      red_laser_detection.valid ? "YES" : "NO",
                                      red_laser_detection.peak_dominance,
                                      red_laser_detection.peak_red,
                                      control_pitch, control_yaw,
                                      laser_aim_cmd.error_x, laser_aim_cmd.error_y),
                          1, laser_aim_cmd.state == aim_follow::LaserAimState::Locked
                              ? cv::Scalar(0, 255, 0)
                              : cv::Scalar(255, 255, 255));
        draw_control_text(fmt::format("Chassis tracking: motor1={} motor2={}  steer:{} distance:{}",
                                      control_motor1, control_motor2,
                                      aim_follow_chassis_steer_enable ? "ON" : "OFF",
                                      aim_follow_distance_enable ? "ON" : "OFF"),
                          2, cv::Scalar(255, 255, 255));
        draw_control_text(fmt::format("CAN output: {}  Gimbal:{}  Chassis:{}",
                                      is_can_dry_run_enabled() ? "DRYRUN(no write)" : "ACTIVE(write can0)",
                                      aim_follow_gimbal_enable ? "ON" : "OFF",
                                      aim_follow_chassis_enable ? "ON" : "OFF"),
                          3, is_can_dry_run_enabled() ? cv::Scalar(0, 255, 255) : cv::Scalar(0, 0, 255));

        if (aim_follow_bytetrack_enable) {
            for (const auto &tracked_box : tracked_display_boxes) {
                const int track_id = tracked_box.first;
                const cv::Rect2f &box = tracked_box.second;
                const bool is_selected = has_control_target && track_id == selected_track_id;
                const cv::Scalar color = is_selected
                    ? cv::Scalar(0, 255, 0)
                    : cv::Scalar(0, 215, 255);
                cv::rectangle(cvmat_to_draw, box, color,
                              is_selected ? 4 : 2, cv::LINE_AA);

                const std::string track_label = fmt::format("ID:{}", track_id);
                int baseline = 0;
                const double font_scale = 0.8;
                const int text_thickness = 2;
                const cv::Size text_size = cv::getTextSize(
                    track_label, cv::FONT_HERSHEY_DUPLEX,
                    font_scale, text_thickness, &baseline);
                const int label_x = std::clamp(
                    static_cast<int>(box.x) + 4,
                    0,
                    std::max(0, cvmat_to_draw.cols - text_size.width - 10));
                const int label_top = std::clamp(
                    static_cast<int>(box.y) + 4,
                    0,
                    std::max(0, cvmat_to_draw.rows - text_size.height - baseline - 10));
                const cv::Rect label_background(
                    label_x,
                    label_top,
                    text_size.width + 10,
                    text_size.height + baseline + 8);
                cv::rectangle(cvmat_to_draw, label_background, color, cv::FILLED);
                cv::putText(cvmat_to_draw, track_label,
                            cv::Point(label_x + 5, label_top + text_size.height + 2),
                            cv::FONT_HERSHEY_DUPLEX, font_scale,
                            cv::Scalar(0, 0, 0), text_thickness, cv::LINE_AA);
            }
        }

        if (aim_follow_laser_aim_enable && red_laser_detection.valid) {
            const cv::Point laser_point(
                static_cast<int>(std::round(red_laser_detection.center.x)),
                static_cast<int>(std::round(red_laser_detection.center.y)));
            cv::circle(cvmat_to_draw, laser_point, 12,
                       cv::Scalar(255, 255, 255), 3, cv::LINE_AA);
            cv::circle(cvmat_to_draw, laser_point, 8,
                       cv::Scalar(0, 0, 255), 2, cv::LINE_AA);
            if (has_control_target) {
                const cv::Point target_center(
                    static_cast<int>(std::round(control_display_box.x +
                                                control_display_box.width * 0.5f)),
                    static_cast<int>(std::round(control_display_box.y +
                                                control_display_box.height * 0.5f)));
                cv::line(cvmat_to_draw, laser_point, target_center,
                         cv::Scalar(0, 255, 255), 2, cv::LINE_AA);
            }
        }

        if (false && target_index >= 0) {
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

            // 归一化 [-1,1]
            float norm_x = -(cx - yolov5_cfg.FRAME_W / 2.0f) / (yolov5_cfg.FRAME_W / 2.0f);
            float norm_y = -(cy - yolov5_cfg.FRAME_H / 2.0f) / (yolov5_cfg.FRAME_H / 2.0f);

            static auto last_can_time = std::chrono::steady_clock::now(); // 记录上次发送时间

            int target_yaw   = static_cast<int>(CENTER_YAW + norm_x*50);
            int target_pitch = static_cast<int>(CENTER_PITCH + norm_y*50);

            target_yaw   = std::clamp(target_yaw, CMD_YAW_MIN, CMD_YAW_MAX);
            target_pitch = std::clamp(target_pitch, CMD_PITCH_MIN, CMD_PITCH_MAX);

            // ----------------- 改进死区逻辑 -----------------
            int send_yaw = last_yaw;
            int send_pitch = last_pitch;

            // 目标是否超过内死区
            if (std::abs(target_yaw - last_yaw) >= DEADZONE_INNER) {
                send_yaw = target_yaw;
            }

            if (std::abs(target_pitch - last_pitch) >= DEADZONE_INNER) {
                send_pitch = target_pitch;
            }

            const auto CAN_INTERVAL = 500ms; // 最小间隔，可调整，100ms = 10Hz
            auto now = std::chrono::steady_clock::now();
            if ((send_yaw != last_yaw || send_pitch != last_pitch) && now - last_can_time >= CAN_INTERVAL) {
                send_can(send_pitch, send_yaw);
                last_yaw = send_yaw;
                last_pitch = send_pitch;
                last_can_time = now;
            }



        }




        // Draw in HDMI coordinates, but calculate distance from the matching
        // detector/model-space width used by the original focal calibration.
        for (int index = 0; index < static_cast<int>(box_list.size()); ++index) {
            const cv::Rect2f &display_box = display_box_list[index];
            const int id = id_list[index];
            cv::rectangle(cvmat_to_draw, display_box, classColor(id), BOX_THICKNESS, cv::LINE_AA);

            const std::string class_label =
                yolov5_cfg.LABELS[id] + ":" + std::to_string(int(std::round(score_list[index] * 100))) + "%";
            const cv::Point2f class_origin = display_box.tl() - cv::Point2f(0, 5);
            cv::putText(cvmat_to_draw, class_label, class_origin, cv::FONT_HERSHEY_DUPLEX,
                        1.0, cv::Scalar(0, 0, 0), 3, cv::LINE_AA);
            cv::putText(cvmat_to_draw, class_label, class_origin, cv::FONT_HERSHEY_DUPLEX,
                        1.0, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);

            const float model_box_width = box_list[index].width;
            if (model_box_width < DISTANCE_MIN_BOX_WIDTH_PX) {
                continue;
            }

            const float raw_distance_m =
                distance_target_real_width_m * distance_camera_focal_px / model_box_width;
            const bool is_selected_target =
                index == target_index && target_filtered_distance_m > 0.0f;
            const float displayed_distance_m = is_selected_target
                ? target_filtered_distance_m
                : raw_distance_m;
            const std::string distance_label = fmt::format("D:{:.2f}m", displayed_distance_m);
            const cv::Point distance_origin(
                static_cast<int>(display_box.tl().x),
                std::max(35, static_cast<int>(display_box.tl().y) - 35));

            cv::putText(cvmat_to_draw, distance_label, distance_origin, cv::FONT_HERSHEY_DUPLEX,
                        1.2, cv::Scalar(0, 0, 0), 5, cv::LINE_AA);
            cv::putText(cvmat_to_draw, distance_label, distance_origin, cv::FONT_HERSHEY_DUPLEX,
                        1.2, cv::Scalar(0, 0, 255), 2, cv::LINE_AA);
        }

        // Legacy draw path retained only as a source reference during migration.
        #if 0
        for (int index = 0; index < box_list.size(); ++index)
        {
            float x1, y1, w, h;
            if (is_hw_resize) // 先crop再resize
            {
                // std::cout << "hardware resize=" << is_hw_resize << ", RATIO_W=" << RATIO_W << ", RATIO_H=" << RATIO_H << ", BIAS_W=" << BIAS_W << ", BIAS_H=" << BIAS_H << std::endl;
                // std::cout << "Original box(xywh): [" << box_list[index].tl().x << "," << box_list[index].tl().y << "," << box_list[index].width << "," << box_list[index].height << "]" << std::endl;
                x1 = box_list[index].tl().x * RATIO_W + BIAS_W;
                y1 = box_list[index].tl().y * RATIO_H + BIAS_H;
                w = box_list[index].width * RATIO_W;
                h = box_list[index].height * RATIO_H;
            }
            else // 先resize再pad
            {
                x1 = (box_list[index].tl().x - BIAS_W) / RATIO_W;
                y1 = (box_list[index].tl().y - BIAS_H) / RATIO_H;
                w = box_list[index].width / RATIO_W;
                h = box_list[index].height / RATIO_H;
            }
            int id = id_list[index];
            cv::Scalar color = classColor(id);
            double font_scale = 1;
            int thickness = 1;
            // cv::rectangle(cvmat_to_draw, cv::Rect2f(x1, y1, w, h), color, 2, cv::LINE_8, 0);//将该行修改成框线宽可调，线宽从固定 2 改成 BOX_THICKNESS，cv::LINE_8 改成 cv::LINE_AA，边缘更平滑
            cv::rectangle(cvmat_to_draw, cv::Rect2f(x1, y1, w, h), color, BOX_THICKNESS, cv::LINE_AA, 0);

            // std::cout << "Draw box(xywh): [" << x1 << "," << y1 << "," << w << "," << h << "] for class " << yolov5_cfg.LABELS[id] << std::endl;
            std::string s = yolov5_cfg.LABELS[id_list[index]] + ":" + std::to_string(int(round(score_list[index] * 100))) + "%";
            cv::Size s_size = cv::getTextSize(s, cv::FONT_HERSHEY_COMPLEX, font_scale, thickness, 0);
            cv::putText(cvmat_to_draw, s, cv::Point(x1, y1 - 5), cv::FONT_HERSHEY_DUPLEX, font_scale, cv::Scalar(255, 255, 255), thickness);
        }
        #endif
        auto bgr_color = cv::Scalar(0, 0, 0);


//-------------------------------------------------------加入瞄准镜绘制----------------------------------------------------------------
            if (RETICLE_ENABLE) {
                int reticle_cx = yolov5_cfg.FRAME_W / 2 + RETICLE_CENTER_X_OFFSET;
                int reticle_cy = yolov5_cfg.FRAME_H / 2 + RETICLE_CENTER_Y_OFFSET;

                draw_reticle(cvmat_to_draw,
                             reticle_cx,
                             reticle_cy,
                             RETICLE_RADIUS,
                             RETICLE_CIRCLE_THICKNESS,
                             RETICLE_LINE_THICKNESS,
                             RETICLE_CROSS_HALF,
                             RETICLE_GAP,
                             RETICLE_TICK_STEP,
                             RETICLE_TICK_COUNT,
                             RETICLE_TICK_LEN,
                             RETICLE_COLOR);
            }
//-------------------------------------------------------加入瞄准镜绘制----------------------------------------------------------------



        drawTextOnTwoCorners(cvmat_to_draw, fmt::format("FPS: {:.1f}", fps_calc.getFPS()), DEMO_NAME, bgr_color);
        // DEBUG code, should be removed later
        // cv::imwrite("output_source_" + std::to_string(source_id) + "_" + get_timestamp_string() + ".jpg", cvmat_to_draw);
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
    Device::Close(device);
    close_can_socket();

    return 0;
}
