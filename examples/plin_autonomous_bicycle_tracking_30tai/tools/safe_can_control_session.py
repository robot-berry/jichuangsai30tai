#!/usr/bin/env python3
import argparse
import fcntl
import json
import os
import signal
import socket
import struct
import subprocess
import time


CAN_FRAME = struct.Struct("=IB3x8s")
CAN_ID_COMMAND = 0x201
CAN_ID_FEEDBACK = 0x212
GIMBAL_NEUTRAL = 100
LOCK_PATH = "/tmp/plin_aim_follow_can.lock"


def run_ip(*args):
    subprocess.run(["ip", *args], check=True)


def payload(motor1, motor2, enable=True):
    return struct.pack(
        ">hhBBBB",
        motor1,
        motor2,
        GIMBAL_NEUTRAL,
        GIMBAL_NEUTRAL,
        0,
        int(enable),
    )


def send_can(sock, data):
    sock.send(CAN_FRAME.pack(CAN_ID_COMMAND, 8, data))


def drain_feedback(sock, state):
    try:
        while True:
            raw = sock.recv(CAN_FRAME.size)
            can_id, dlc, data = CAN_FRAME.unpack(raw)
            if can_id & 0x1FFFFFFF == CAN_ID_FEEDBACK and dlc >= 7:
                state["mode"] = data[0]
                state["motor1"], state["motor2"] = struct.unpack(">hh", data[1:5])
                state["pitch"] = data[5]
                state["bearing"] = data[6]
                state["feedback_at"] = time.monotonic()
    except (BlockingIOError, socket.timeout):
        pass


def write_status(path, state, active_motor1, active_motor2, active_until):
    status = {
        "mode": state["mode"],
        "mode_hex": None if state["mode"] is None else "0x%02X" % state["mode"],
        "feedback_motor1": state["motor1"],
        "feedback_motor2": state["motor2"],
        "gimbal_feedback": [state["pitch"], state["bearing"]],
        "command_motor1": active_motor1,
        "command_motor2": active_motor2,
        "pulse_active": time.monotonic() < active_until,
        "updated_at": time.strftime("%H:%M:%S"),
    }
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="ascii") as output:
        json.dump(status, output, sort_keys=True)
    os.replace(tmp, path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bitrate", type=int, default=250000)
    parser.add_argument("--max-rpm", type=int, default=50)
    parser.add_argument("--max-pulse", type=float, default=0.2)
    parser.add_argument("--socket", default="/tmp/plin_safe_can_control.sock")
    parser.add_argument("--status", default="/tmp/plin_safe_can_status.json")
    args = parser.parse_args()

    if not 1 <= args.max_rpm <= 50:
        raise SystemExit("max-rpm must be between 1 and 50")
    if not 0.02 <= args.max_pulse <= 0.2:
        raise SystemExit("max-pulse must be between 0.02 and 0.2 seconds")

    stop_requested = False

    def stop(_signum, _frame):
        nonlocal stop_requested
        stop_requested = True

    for signum in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(signum, stop)

    can_sock = None
    control_sock = None
    zero_enabled = payload(0, 0, True)
    zero_disabled = payload(0, 0, False)
    state = {
        "mode": None,
        "motor1": None,
        "motor2": None,
        "pitch": None,
        "bearing": None,
        "feedback_at": 0.0,
    }

    with open(LOCK_PATH, "a+", encoding="ascii") as lock_file:
        fcntl.flock(lock_file.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        try:
            run_ip("link", "set", "can0", "down")
            run_ip(
                "link", "set", "can0", "type", "can", "bitrate",
                str(args.bitrate), "restart-ms", "100"
            )
            run_ip("link", "set", "can0", "up")

            can_sock = socket.socket(socket.PF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
            can_sock.bind(("can0",))
            can_sock.setblocking(False)

            # The chassis can remain in remote-control mode (0x55) after the
            # handset is switched off.  Clear that ownership first, then use
            # zero-speed enable heartbeats to request CAN mode (0xAA).
            for _ in range(10):
                send_can(can_sock, zero_disabled)
                drain_feedback(can_sock, state)
                time.sleep(0.02)

            mode_before = state["mode"]
            print(
                "[SESSION PREPARE] mode_before=%s; disable_frames=10; "
                "gimbal_velocity=100/100"
                % (
                    "unknown"
                    if mode_before is None
                    else "0x%02X" % mode_before,
                ),
                flush=True,
            )

            handshake_deadline = time.monotonic() + 2.0
            while time.monotonic() < handshake_deadline and not stop_requested:
                send_can(can_sock, zero_enabled)
                drain_feedback(can_sock, state)
                if state["mode"] == 0xAA:
                    break
                time.sleep(0.02)
            if state["mode"] != 0xAA:
                raise RuntimeError("CAN mode handshake failed; session cancelled")

            if os.path.exists(args.socket):
                os.unlink(args.socket)
            control_sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
            control_sock.bind(args.socket)
            control_sock.setblocking(False)
            os.chmod(args.socket, 0o600)

            print("[SESSION READY] mode=0xAA; motors=0/0", flush=True)
            active_motor1 = 0
            active_motor2 = 0
            active_until = 0.0
            next_status = 0.0

            while not stop_requested:
                now = time.monotonic()
                try:
                    message = control_sock.recv(1024)
                    command = json.loads(message.decode("ascii"))
                    motor1 = int(command["motor1"])
                    motor2 = int(command["motor2"])
                    duration = float(command["duration"])
                    if max(abs(motor1), abs(motor2)) > args.max_rpm:
                        raise ValueError("motor command exceeds safety limit")
                    if not 0.02 <= duration <= args.max_pulse:
                        raise ValueError("pulse duration exceeds safety limit")
                    active_motor1 = motor1
                    active_motor2 = motor2
                    active_until = now + duration
                    print(
                        "[PULSE] motor1=%d motor2=%d duration=%.3f"
                        % (motor1, motor2, duration),
                        flush=True,
                    )
                except BlockingIOError:
                    pass
                except (KeyError, TypeError, ValueError, json.JSONDecodeError) as exc:
                    print("[COMMAND REJECTED] %s" % exc, flush=True)

                drain_feedback(can_sock, state)
                feedback_fresh = now - state["feedback_at"] <= 0.30
                mode_ready = state["mode"] == 0xAA
                pulse_active = now < active_until and feedback_fresh and mode_ready
                if pulse_active:
                    send_can(can_sock, payload(active_motor1, active_motor2, True))
                else:
                    active_motor1 = 0
                    active_motor2 = 0
                    active_until = 0.0
                    send_can(can_sock, zero_enabled)

                if now >= next_status:
                    write_status(args.status, state, active_motor1, active_motor2, active_until)
                    next_status = now + 0.10
                time.sleep(0.02)
        finally:
            if can_sock is not None:
                for _ in range(10):
                    try:
                        send_can(can_sock, zero_enabled)
                    except OSError:
                        pass
                    time.sleep(0.02)
                for _ in range(5):
                    try:
                        send_can(can_sock, zero_disabled)
                    except OSError:
                        pass
                    time.sleep(0.02)
                can_sock.close()
            if control_sock is not None:
                control_sock.close()
            if os.path.exists(args.socket):
                os.unlink(args.socket)
            subprocess.run(["ip", "link", "set", "can0", "down"], check=False)
            print("[SESSION STOP] motors zero; CAN disabled and down", flush=True)


if __name__ == "__main__":
    main()
