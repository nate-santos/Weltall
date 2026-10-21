"""Synthetic reports verify that unrelated warnings or partial runs never become a pass."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Scripts"))
import android_audio_policy as policy


def report(warnings=()):
    return {"succeeded": 0 if warnings else 1, "succeededWithWarnings": 1 if warnings else 0,
            "failed": 0, "notRun": 0, "inProcess": 0,
            "tests": [{"fullTestPath": policy.TEST, "state": "Success", "entries": [
                {"event": {"type": "Warning", "message": message}} for message in warnings]}]}


class AudioPolicyReportTests(unittest.TestCase):
    def test_delayed_case_must_match_its_exact_requested_identity(self):
        value = report()
        value["tests"][0]["fullTestPath"] = policy.DELAYED_TEST
        self.assertFalse(policy.assess_report(value)[0])
        self.assertTrue(policy.assess_report(value, policy.DELAYED_TEST)[0])

    def test_only_reviewed_engine_warnings_are_retained_and_accepted(self):
        messages = ["LogAudioMixer: AudioMixerPlatformInterface Timeout [ 5 Seconds] waiting for h/w. InstanceID=1"]
        self.assertEqual(policy.assess_report(report(messages)), (True, messages))
        self.assertTrue(policy.assess_report(report())[0])
        self.assertFalse(policy.assess_report(report(messages + ["An unreviewed runtime warning"]))[0])

    def test_failures_wrong_case_incomplete_or_unexplained_counts_are_rejected(self):
        for change in ({"failed": 1}, {"notRun": 1}, {"inProcess": 1}, {"tests": []},
                       {"succeeded": 0, "succeededWithWarnings": 1},
                       {"tests": [{"fullTestPath": "Another.Test", "state": "Success"}]}):
            with self.subTest(change=change):
                self.assertFalse(policy.assess_report(report() | change)[0])


if __name__ == "__main__":
    unittest.main()
