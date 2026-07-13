#!/usr/bin/env python3
import argparse
import json
import socket


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--socket", default="/tmp/plin_safe_can_control.sock")
    parser.add_argument("--motor1", type=int, required=True)
    parser.add_argument("--motor2", type=int, required=True)
    parser.add_argument("--duration", type=float, required=True)
    args = parser.parse_args()

    message = json.dumps(
        {"motor1": args.motor1, "motor2": args.motor2, "duration": args.duration}
    ).encode("ascii")
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    sock.sendto(message, args.socket)
    sock.close()


if __name__ == "__main__":
    main()
