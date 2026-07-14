#!/usr/bin/env python3
import os
import sys
import unittest


sys.path.insert(0, os.path.dirname(__file__))
from safe_tracking_bridge import effective_command, parse_control_line, validate_command


class SafeTrackingBridgeTest(unittest.TestCase):
    def test_follow_turn_and_distance_commands(self):
        left = parse_control_line(
            "[AIM FOLLOW] cx=500 cy=500 motor1=-21 motor2=21 "
            "forward=0 steer=-21 pitch=150 yaw=123 ex=-0.4 ey=0"
        )
        self.assertEqual(left["state"], "TRACK")
        self.assertEqual(validate_command(left, 25, 40), (-21, 21))

        backward = parse_control_line(
            "[AIM FOLLOW] cx=960 cy=540 motor1=-18 motor2=-18 "
            "forward=-18 steer=0 pitch=150 yaw=123 ex=0 ey=0"
        )
        self.assertEqual(validate_command(backward, 25, 40), (-18, -18))

    def test_search_and_hold_commands(self):
        search = parse_control_line(
            "[AIM SEARCH] searching=1 motor1=-40 motor2=40 steer=-40"
        )
        self.assertEqual(search["state"], "SEARCH")
        self.assertEqual(validate_command(search, 25, 40), (-40, 40))

        hold = parse_control_line(
            "[AIM SEARCH] searching=0 motor1=0 motor2=0 steer=0"
        )
        self.assertEqual(hold["state"], "HOLD")
        self.assertEqual(validate_command(hold, 25, 40), (0, 0))

    def test_rejects_unsafe_commands(self):
        with self.assertRaises(ValueError):
            validate_command(
                {"state": "TRACK", "motor1": 51, "motor2": -51}, 50, 40
            )
        with self.assertRaises(ValueError):
            validate_command(
                {"state": "SEARCH", "motor1": 40, "motor2": 40}, 25, 40
            )

    def test_search_requires_continuous_loss_confirmation(self):
        search = {
            "state": "SEARCH",
            "motor1": -40,
            "motor2": 40,
            "forward": 0,
            "steer": -40,
        }
        self.assertEqual(
            effective_command(search, 10.0, 10.2, 0.35),
            ("SEARCH_WAIT", 0, 0),
        )
        self.assertEqual(
            effective_command(search, 10.0, 10.4, 0.35),
            ("SEARCH", -40, 40),
        )


if __name__ == "__main__":
    unittest.main()
