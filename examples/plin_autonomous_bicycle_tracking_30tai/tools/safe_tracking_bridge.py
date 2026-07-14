#!/usr/bin/env python3
import argparse
import fcntl
import json
import os
import re
import socket
import time


FOLLOW_RE = re.compile(
    r"\[AIM FOLLOW\].*?motor1=(-?\d+)\s+motor2=(-?\d+).*?"
    r"forward=(-?\d+)\s+steer=(-?\d+)"
)
SEARCH_RE = re.compile(
    r"\[AIM SEARCH\]\s+searching=([01])\s+motor1=(-?\d+)\s+"
    r"motor2=(-?\d+)\s+steer=(-?\d+)"
)
LOCK_PATH = "/tmp/plin_safe_tracking_bridge.lock"


def parse_control_line(line):
    match = FOLLOW_RE.search(line)
    if match:
        motor1, motor2, forward, steer = (int(value) for value in match.groups())
        return {
            "state": "TRACK",
            "motor1": motor1,
            "motor2": motor2,
            "forward": forward,
            "steer": steer,
        }

    match = SEARCH_RE.search(line)
    if match:
        searching, motor1, motor2, steer = (int(value) for value in match.groups())
        return {
            "state": "SEARCH" if searching else "HOLD",
            "motor1": motor1,
            "motor2": motor2,
            "forward": 0,
            "steer": steer,
        }
    return None


def validate_command(command, track_rpm_limit, search_rpm_limit):
    motor1 = command["motor1"]
    motor2 = command["motor2"]
    state = command["state"]
    limit = search_rpm_limit if state == "SEARCH" else track_rpm_limit
    if max(abs(motor1), abs(motor2)) > limit:
        raise ValueError("%s command exceeds %d rpm limit" % (state, limit))
    if state == "SEARCH" and motor1 * motor2 >= 0:
        raise ValueError("search command must be a differential turn")
    if state == "HOLD" and (motor1 != 0 or motor2 != 0):
        raise ValueError("hold command must be zero")
    return motor1, motor2


def effective_command(command, search_since, now, search_confirm):
    if (
        command["state"] == "SEARCH"
        and search_since is not None
        and now - search_since < search_confirm
    ):
        return "SEARCH_WAIT", 0, 0
    return command["state"], command["motor1"], command["motor2"]


def send_command(socket_path, motor1, motor2, duration):
    message = json.dumps(
        {"motor1": motor1, "motor2": motor2, "duration": duration}
    ).encode("ascii")
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    try:
        sock.sendto(message, socket_path)
    finally:
        sock.close()


def safe_session_ready(socket_path, status_path, max_age=1.5):
    if not os.path.exists(socket_path) or not os.path.exists(status_path):
        return False
    if time.time() - os.path.getmtime(status_path) > max_age:
        return False
    try:
        with open(status_path, "r", encoding="ascii") as source:
            status = json.load(source)
    except (OSError, ValueError):
        return False
    return status.get("mode") == 0xAA


def emit(output, message):
    output.write(message + "\n")
    output.flush()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--log", required=True)
    parser.add_argument("--socket", default="/tmp/plin_safe_can_control.sock")
    parser.add_argument("--status", default="/tmp/plin_safe_can_status.json")
    parser.add_argument("--track-rpm-limit", type=int, default=50)
    parser.add_argument("--search-rpm-limit", type=int, default=40)
    parser.add_argument("--pulse", type=float, default=0.15)
    parser.add_argument("--send-period", type=float, default=0.10)
    parser.add_argument("--stale-timeout", type=float, default=0.35)
    parser.add_argument("--search-confirm", type=float, default=0.35)
    parser.add_argument("--from-start", action="store_true")
    parser.add_argument("--arm", action="store_true")
    parser.add_argument("--max-runtime", type=float, default=0.0)
    args = parser.parse_args()

    if not 1 <= args.track_rpm_limit <= 50:
        raise SystemExit("track-rpm-limit must be between 1 and 50")
    if not 1 <= args.search_rpm_limit <= 40:
        raise SystemExit("search-rpm-limit must be between 1 and 40")
    if not 0.05 <= args.pulse <= 0.2:
        raise SystemExit("pulse must be between 0.05 and 0.2 seconds")
    if args.send_period >= args.pulse:
        raise SystemExit("send-period must be shorter than pulse")
    if not 0.1 <= args.search_confirm <= 2.0:
        raise SystemExit("search-confirm must be between 0.1 and 2.0 seconds")

    start_time = time.monotonic()
    last_event_at = 0.0
    last_send_at = 0.0
    last_command = None
    stale_zero_sent = False
    search_since = None
    session_unready_since = None

    with open(LOCK_PATH, "a+", encoding="ascii") as lock_file:
        fcntl.flock(lock_file.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        while not os.path.exists(args.log):
            if args.max_runtime and time.monotonic() - start_time >= args.max_runtime:
                return
            time.sleep(0.05)

        with open(args.log, "r", encoding="utf-8", errors="replace") as source:
            if not args.from_start:
                source.seek(0, os.SEEK_END)
            emit(
                output=os.sys.stdout,
                message="[BRIDGE READY] mode=%s gimbal=disabled"
                % ("ARMED" if args.arm else "DRYRUN"),
            )

            while True:
                now = time.monotonic()
                if args.max_runtime and now - start_time >= args.max_runtime:
                    break

                line = source.readline()
                if line:
                    command = parse_control_line(line)
                    if command is not None:
                        validate_command(
                            command, args.track_rpm_limit, args.search_rpm_limit
                        )
                        last_command = command
                        last_event_at = now
                        stale_zero_sent = False
                        if command["state"] == "SEARCH":
                            if search_since is None:
                                search_since = now
                        else:
                            search_since = None
                else:
                    time.sleep(0.01)

                fresh = last_command is not None and now - last_event_at <= args.stale_timeout
                if fresh and now - last_send_at >= args.send_period:
                    state, motor1, motor2 = effective_command(
                        last_command,
                        search_since,
                        now,
                        args.search_confirm,
                    )
                    if args.arm:
                        if not safe_session_ready(args.socket, args.status):
                            if session_unready_since is None:
                                session_unready_since = now
                                emit(os.sys.stdout, "[BRIDGE CAN WAIT] motors=0/0")
                            elif now - session_unready_since >= 2.0:
                                raise RuntimeError(
                                    "safe CAN session unavailable for 2 seconds"
                                )
                            last_send_at = now
                            continue
                        session_unready_since = None
                        send_command(args.socket, motor1, motor2, args.pulse)
                    emit(
                        os.sys.stdout,
                        "[BRIDGE %s] state=%s motor1=%d motor2=%d"
                        % (
                            "SEND" if args.arm else "DRYRUN",
                            state,
                            motor1,
                            motor2,
                        ),
                    )
                    last_send_at = now
                elif not fresh and last_event_at > 0.0 and not stale_zero_sent:
                    if args.arm and safe_session_ready(args.socket, args.status):
                        send_command(args.socket, 0, 0, args.pulse)
                    emit(os.sys.stdout, "[BRIDGE STALE] motors=0/0")
                    stale_zero_sent = True

        if args.arm and safe_session_ready(args.socket, args.status):
            send_command(args.socket, 0, 0, args.pulse)
        emit(os.sys.stdout, "[BRIDGE STOP] motors=0/0")


if __name__ == "__main__":
    main()
