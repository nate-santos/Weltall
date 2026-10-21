"""Generated inputs only: no engine, discs, saves or game captures are read."""

from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))
import render_game_reference as reference


class GameReferenceTests(unittest.TestCase):
    def test_late_actions_keep_exact_timing_beyond_the_command_line_limit(self):
        script = reference.pad_script(3600, [
            {"at": 2500, "buttons": "Left"},
            {"at": 2501, "buttons": "Cross"},
            {"at": 2502, "buttons": "L2+R2", "duration": .2},
        ], 1)
        self.assertGreater(len(script), 16384)
        steps = script.split(",")
        self.assertEqual(steps[25000:25003], ["Left", "", ""])
        self.assertEqual(steps[25010], "Cross")
        self.assertEqual(steps[25020:], ["L2+R2", "L2+R2"])

    def test_disc_two_loading_uses_normal_confirmation_buttons(self):
        first = reference.pad_script(40, [], 1).split(",")
        second = reference.pad_script(40, [], 2).split(",")
        self.assertEqual(first[280], "Left")
        self.assertEqual(first[290], "Cross")
        self.assertEqual(len(first), 311)
        self.assertEqual(second[330], "Cross")
        self.assertEqual(second[350], "Cross")

    def test_ambiguous_or_invalid_actions_are_rejected_before_play(self):
        invalid = [
            [{"at": 35, "buttons": "Cross"}],
            [{"at": 36.01, "buttons": "Cross"}],
            [{"at": True, "buttons": "Cross"}],
            [{"at": float("nan"), "buttons": "Cross"}],
            [{"at": float("inf"), "buttons": "Cross"}],
            [{"at": 36, "buttons": "Cross", "duration": 0}],
            [{"at": 39.9, "buttons": "Cross", "duration": .2}],
            [{"at": 36, "buttons": "Cross", "duration": .2}, {"at": 36.1, "buttons": "Circle"}],
            [{"at": 36, "buttons": "Cross*0.5"}],
            [{"at": 36, "buttons": "TurnLeft*nan"}],
            [{"at": 36, "buttons": "TurnLeft*1.5"}],
            [{"at": 36, "buttons": "NotAButton"}],
            [{"at": 36, "buttons": "Cross", "stage_game_state": True}],
        ]
        for actions in invalid:
            with self.subTest(actions=actions), self.assertRaises(ValueError):
                reference.pad_script(40, actions, 1)

    def test_transition_summary_keeps_stops_and_requires_visual_review(self):
        root = reference.REPO / "out/python-tests"
        root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=root) as temporary:
            folder = Path(temporary)
            for name in ("frame-040.00.png", "frame-060.00.png"):
                (folder/name).touch()
            log = folder / "editor.log"
            log.write_text(
                "GameTrace 40.000 stage=1 program=1 pad=0000\n"
                "GameTrace field map=1 leader=0 position=0,0,0 lock=FFFF yaw=0 disc=1 movie=-1\n"
                "GameTrace 42.000 stage=1 program=1 pad=0000\n"
                "GameTrace field map=1 leader=0 position=0,0,0 lock=FFFF yaw=0 disc=1 movie=2\n"
                "GameTrace 44.000 stage=1 program=1 pad=0000\n"
                "GameTrace field map=1 leader=0 position=0,0,0 lock=FFFF yaw=0 disc=2 movie=-1\n"
                "GameTrace 46.000 stage=3 program=1 pad=0000\n"
                "LogExample: Error: generated failure\n", encoding="utf-8")
            result = reference.summarize_log(log, folder)
            self.assertTrue(result["stopped_program"])
            self.assertTrue(result["visual_review_required"])
            self.assertEqual(result["gameplay_acceptance"], "not assessed")
            fields = [event for event in result["transitions"] if event["kind"] == "field"]
            self.assertEqual([(event["disc"], event["movie"]) for event in fields], [(1, -1), (1, 2), (2, -1)])
            self.assertEqual(fields[1]["review_before"], "frame-040.00.png")
            self.assertEqual(fields[1]["review_after"], "frame-060.00.png")
            self.assertEqual(result["logged_errors"], ["LogExample: Error: generated failure"])


if __name__ == "__main__":
    unittest.main()
