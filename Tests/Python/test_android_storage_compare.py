"""Generated CSV metrics; no device or game inputs."""
import csv
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'Scripts'))
from android_storage_compare import analyze


class StorageComparisonTests(unittest.TestCase):
    def setUp(self):
        self.root = Path(__file__).resolve().parents[2]/'out/python-tests'
        self.root.mkdir(parents=True, exist_ok=True)

    def test_stalls_and_zero_gpu_frames_are_retained(self):
        with tempfile.TemporaryDirectory(dir=self.root) as root:
            path = Path(root)/'profile.csv'
            with path.open('w', newline='') as stream:
                writer = csv.writer(stream)
                writer.writerow(['FrameTime', 'GameThreadTime', 'RenderThreadTime', 'RHIThreadTime', 'GPUTime', 'WeltallDiscIO/Requests'])
                writer.writerows([[16, 2, 3, 4, 0, 1], [200, 1, 1, 1, 4, 2], ['[metadata]']])
            result = analyze(path)
            self.assertEqual(result['numeric_frames'], 2)
            self.assertEqual(result['frames_over_100ms'], 1)
            self.assertEqual(result['io']['WeltallDiscIO/Requests']['total'], 3)
            self.assertAlmostEqual(result['fps'], 2/.216)

    def test_invalid_numeric_data_fails(self):
        with tempfile.TemporaryDirectory(dir=self.root) as root:
            path = Path(root)/'profile.csv'
            path.write_text('FrameTime,GameThreadTime,RenderThreadTime,RHIThreadTime,GPUTime\n16,1,1,1,nan\n')
            with self.assertRaisesRegex(ValueError, 'invalid CSV'):
                analyze(path)

    def test_complete_header_at_end_handles_new_counters(self):
        with tempfile.TemporaryDirectory(dir=self.root) as root:
            path = Path(root)/'profile.csv'
            path.write_text('FrameTime,GameThreadTime,RenderThreadTime,RHIThreadTime,GPUTime\n'
                            '16,1,1,1,0\n200,1,1,1,4,3\n'
                            'FrameTime,GameThreadTime,RenderThreadTime,RHIThreadTime,GPUTime,WeltallDiscIO/Requests\n'
                            '[HasHeaderRowAtEnd],1\n')
            result = analyze(path)
            self.assertEqual(result['numeric_frames'], 2)
            self.assertEqual(result['frames_over_100ms'], 1)
            self.assertEqual(result['io']['WeltallDiscIO/Requests']['total'], 3)


if __name__ == '__main__':
    unittest.main()
