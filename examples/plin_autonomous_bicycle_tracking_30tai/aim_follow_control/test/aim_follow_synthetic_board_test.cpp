#include "aim_follow_controller.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef __linux__
#include <cerrno>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

constexpr uint32_t kChassisCanId = 0x201;
constexpr uint32_t kGimbalCanId = 0x38A;
constexpr int kCanBitrate = 250000;
constexpr uint8_t kTriggerStop = 0x00;
constexpr uint8_t kCanControlEnable = 0x01;

struct Options {
    bool send_can = false;
    bool configure_can = false;
    std::string can_iface = "can0";
    int repeat = 1;
    int sleep_ms = 120;
};

struct Scenario {
    std::string name;
    aim_follow::TargetObservation obs;
};

uint8_t clampPercentByte(int value) {
    return static_cast<uint8_t>(std::clamp(value, 0, 200));
}

int16_t clampMotorRpm(int rpm) {
    return static_cast<int16_t>(std::clamp(rpm, -1000, 1000));
}

std::string bytesToHex(const std::vector<uint8_t> &bytes) {
    std::ostringstream oss;
    oss << std::uppercase << std::hex << std::setfill('0');
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i > 0) {
            oss << ' ';
        }
        oss << std::setw(2) << static_cast<int>(bytes[i]);
    }
    return oss.str();
}

std::vector<uint8_t> buildChassisPayload(int motor1_rpm, int motor2_rpm, int pitch, int yaw) {
    const int16_t motor1 = clampMotorRpm(motor1_rpm);
    const int16_t motor2 = clampMotorRpm(motor2_rpm);
    return {
        static_cast<uint8_t>((motor1 >> 8) & 0xFF),
        static_cast<uint8_t>(motor1 & 0xFF),
        static_cast<uint8_t>((motor2 >> 8) & 0xFF),
        static_cast<uint8_t>(motor2 & 0xFF),
        clampPercentByte(pitch),
        clampPercentByte(yaw),
        kTriggerStop,
        kCanControlEnable,
    };
}

std::vector<uint8_t> buildGimbalPayload(int pitch, int yaw) {
    return {
        0xAA,
        clampPercentByte(pitch),
        clampPercentByte(yaw),
        kTriggerStop,
        0x00,
        0x00,
        0x00,
        0x55,
    };
}

#ifdef __linux__
class SocketCanSender {
public:
    ~SocketCanSender() {
        if (fd_ >= 0) {
            close(fd_);
        }
    }

    bool openInterface(const std::string &iface) {
        fd_ = socket(PF_CAN, SOCK_RAW, CAN_RAW);
        if (fd_ < 0) {
            std::cerr << "[SYNTH CAN] socket failed: " << std::strerror(errno) << std::endl;
            return false;
        }

        struct ifreq ifr {};
        std::strncpy(ifr.ifr_name, iface.c_str(), IFNAMSIZ - 1);
        if (ioctl(fd_, SIOCGIFINDEX, &ifr) < 0) {
            std::cerr << "[SYNTH CAN] ioctl SIOCGIFINDEX failed: " << std::strerror(errno) << std::endl;
            return false;
        }

        struct sockaddr_can addr {};
        addr.can_family = AF_CAN;
        addr.can_ifindex = ifr.ifr_ifindex;
        if (bind(fd_, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
            std::cerr << "[SYNTH CAN] bind failed: " << std::strerror(errno) << std::endl;
            return false;
        }
        return true;
    }

    bool sendFrame(uint32_t can_id, const std::vector<uint8_t> &payload) {
        if (fd_ < 0) {
            return false;
        }

        struct can_frame frame {};
        frame.can_id = can_id;
        frame.can_dlc = static_cast<__u8>(std::min<size_t>(payload.size(), 8));
        for (int i = 0; i < frame.can_dlc; ++i) {
            frame.data[i] = payload[static_cast<size_t>(i)];
        }

        const ssize_t nbytes = write(fd_, &frame, sizeof(frame));
        if (nbytes != sizeof(frame)) {
            std::cerr << "[SYNTH CAN] send 0x" << std::hex << can_id << std::dec
                      << " failed: " << std::strerror(errno) << std::endl;
            return false;
        }
        return true;
    }

private:
    int fd_ = -1;
};
#endif

void printUsage(const char *argv0) {
    std::cout << "Usage: " << argv0 << " [--send-can] [--configure-can] "
              << "[--can-iface can0] [--repeat N] [--sleep-ms N]" << std::endl;
}

Options parseArgs(int argc, char **argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--send-can") {
            options.send_can = true;
        } else if (arg == "--configure-can") {
            options.configure_can = true;
        } else if (arg == "--can-iface" && i + 1 < argc) {
            options.can_iface = argv[++i];
        } else if (arg == "--repeat" && i + 1 < argc) {
            options.repeat = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--sleep-ms" && i + 1 < argc) {
            options.sleep_ms = std::max(0, std::atoi(argv[++i]));
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            std::exit(0);
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            printUsage(argv[0]);
            std::exit(2);
        }
    }
    return options;
}

std::vector<Scenario> buildScenarios() {
    std::vector<Scenario> scenarios;
    const float cx = 960.0f;
    const float cy = 540.0f;
    float t = 0.05f;
    aim_follow::DistanceEstimatorConfig distance_cfg;
    distance_cfg.filter_alpha = 1.0f;
    distance_cfg.median_window_size = 1;
    distance_cfg.stability_deadband_m = 0.0f;
    distance_cfg.max_filtered_step_m = 10.0f;
    aim_follow::MonocularDistanceEstimator distance_estimator(distance_cfg);

    auto makeObs = [&](bool valid, float x, float y, float distance_m) {
        aim_follow::TargetObservation obs;
        obs.valid = valid;
        obs.center_x = x;
        obs.center_y = y;
        if (valid && distance_m > 0.0f) {
            obs.box_width = distance_estimator.config().target_real_width_m *
                            distance_estimator.config().focal_length_px /
                            distance_m;
            obs.distance_m = distance_estimator.update(obs.box_width).filtered_distance_m;
        } else {
            obs.box_width = 0.0f;
            obs.distance_m = -1.0f;
        }
        obs.timestamp_s = t;
        t += 0.05f;
        return obs;
    };

    scenarios.push_back({"center_hold", makeObs(true, cx, cy, 1.0f)});
    scenarios.push_back({"target_far_forward", makeObs(true, cx, cy, 1.6f)});
    scenarios.push_back({"target_close_backward", makeObs(true, cx, cy, 0.55f)});
    scenarios.push_back({"target_right_yaw", makeObs(true, 1500.0f, cy, 1.0f)});
    scenarios.push_back({"target_up_pitch", makeObs(true, cx, 200.0f, 1.0f)});
    for (int i = 0; i < 6; ++i) {
        scenarios.push_back({"target_lost_" + std::to_string(i + 1), makeObs(false, 0.0f, 0.0f, -1.0f)});
    }
    return scenarios;
}

} // namespace

int main(int argc, char **argv) {
    const Options options = parseArgs(argc, argv);

    aim_follow::ControlConfig cfg;
    cfg.frame_width = 1920.0f;
    cfg.frame_height = 1080.0f;
    cfg.target_distance_m = 1.0f;
    cfg.yaw_kp = 38.0f;
    cfg.yaw_kd = 8.0f;
    cfg.pitch_kp = 42.0f;
    cfg.pitch_kd = 8.0f;
    cfg.distance_deadband_m = 0.12f;
    cfg.follow_kp_rpm_per_m = 180.0f;
    cfg.min_follow_rpm = 35;
    cfg.max_follow_rpm = 160;
    cfg.motor1_forward_sign = 1;
    cfg.motor2_forward_sign = 1;

    std::cout << "[SYNTH CONFIG] send_can=" << options.send_can
              << " configure_can=" << options.configure_can
              << " can_iface=" << options.can_iface
              << " repeat=" << options.repeat
              << " sleep_ms=" << options.sleep_ms
              << " target_distance_m=" << cfg.target_distance_m
              << std::endl;

    if (options.configure_can) {
        const std::string cmd =
            "ip link set " + options.can_iface + " down 2>/dev/null || true; "
            "ip link set " + options.can_iface + " type can bitrate " + std::to_string(kCanBitrate) + " restart-ms 100; "
            "ip link set " + options.can_iface + " up";
        const int ret = std::system(cmd.c_str());
        std::cout << "[SYNTH CAN] configure command ret=" << ret << std::endl;
    }

#ifdef __linux__
    SocketCanSender can_sender;
    bool can_ready = false;
    if (options.send_can) {
        can_ready = can_sender.openInterface(options.can_iface);
        std::cout << "[SYNTH CAN] open " << options.can_iface << " ready=" << can_ready << std::endl;
    }
#else
    const bool can_ready = false;
    if (options.send_can) {
        std::cout << "[SYNTH CAN] SocketCAN is only available on Linux" << std::endl;
    }
#endif

    aim_follow::AimFollowController controller(cfg);
    const auto scenarios = buildScenarios();

    bool saw_forward = false;
    bool saw_backward = false;
    bool saw_yaw_change = false;
    bool saw_pitch_change = false;
    bool saw_lost_stop = false;

    for (int round = 0; round < options.repeat; ++round) {
        if (round > 0) {
            controller.reset();
        }

        for (const auto &scenario : scenarios) {
            const auto out = controller.update(scenario.obs);
            const auto chassis = buildChassisPayload(out.motor1_rpm, out.motor2_rpm, out.pitch, out.yaw);
            const auto gimbal = buildGimbalPayload(out.pitch, out.yaw);

            std::cout << "[SYNTH OBS] round=" << round
                      << " case=" << scenario.name
                      << " valid=" << scenario.obs.valid
                      << " cx=" << scenario.obs.center_x
                      << " cy=" << scenario.obs.center_y
                      << " distance=" << scenario.obs.distance_m
                      << std::endl;
            std::cout << "[SYNTH OUTPUT] case=" << scenario.name
                      << " motor1=" << out.motor1_rpm
                      << " motor2=" << out.motor2_rpm
                      << " pitch=" << out.pitch
                      << " yaw=" << out.yaw
                      << " ex=" << out.norm_error_x
                      << " ey=" << out.norm_error_y
                      << " distance_error=" << out.distance_error_m
                      << std::endl;
            std::cout << "[SYNTH CAN 0x201] " << bytesToHex(chassis) << std::endl;
            std::cout << "[SYNTH CAN 0x38A] " << bytesToHex(gimbal) << std::endl;

#ifdef __linux__
            if (options.send_can && can_ready) {
                can_sender.sendFrame(kChassisCanId, chassis);
                can_sender.sendFrame(kGimbalCanId, gimbal);
            }
#endif

            saw_forward = saw_forward || (out.motor1_rpm > 0 && out.motor2_rpm > 0);
            saw_backward = saw_backward || (out.motor1_rpm < 0 && out.motor2_rpm < 0);
            saw_yaw_change = saw_yaw_change || (out.yaw != cfg.center_yaw);
            saw_pitch_change = saw_pitch_change || (out.pitch != cfg.center_pitch);
            saw_lost_stop = saw_lost_stop || (!scenario.obs.valid && out.motor1_rpm == 0 && out.motor2_rpm == 0);

            if (options.sleep_ms > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(options.sleep_ms));
            }
        }
    }

    const bool pass = saw_forward && saw_backward && saw_yaw_change && saw_pitch_change && saw_lost_stop;
    std::cout << "[SYNTH SUMMARY] forward=" << saw_forward
              << " backward=" << saw_backward
              << " yaw_change=" << saw_yaw_change
              << " pitch_change=" << saw_pitch_change
              << " lost_stop=" << saw_lost_stop
              << " result=" << (pass ? "PASS" : "FAIL")
              << std::endl;

    return pass ? 0 : 1;
}
