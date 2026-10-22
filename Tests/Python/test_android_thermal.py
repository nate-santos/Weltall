import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'Scripts'))
from android_thermal import parse_thermal, parse_battery, limit_reason, summarize


class ThermalTests(unittest.TestCase):
    def test_live_values_exclude_stale_cache_and_non_temperature_sensors(self):
        data = '''Thermal Status: 0
Cached temperatures:
 Temperature{mValue=90.0, mType=0, mName=CPU0, mStatus=3}
Current temperatures from HAL:
 Temperature{mValue=27.5, mType=3, mName=skin, mStatus=0}
 Temperature{mValue=0.85, mType=7, mName=ibat, mStatus=0}
Current cooling devices from HAL:
'''
        actual = parse_thermal(data)
        self.assertEqual(actual['temperatures'], [dict(name='skin', type=3, celsius=27.5, status=0)])
        self.assertIsNone(limit_reason(actual, 38))

    def test_missing_live_section_cannot_pass(self):
        for data in ('Thermal Status: 0', 'Current temperatures from HAL:', 'Thermal Status: 0\nCurrent temperatures from HAL:'):
            with self.assertRaises(ValueError):
                parse_thermal(data)

    def test_thermal_and_sensor_and_skin_limits(self):
        sample = dict(status=0, temperatures=[dict(type=3, celsius=38, status=0)])
        self.assertIsNotNone(limit_reason(sample, 38))
        sample['temperatures'][0]['celsius'] = 27
        sample['temperatures'][0]['status'] = 2
        self.assertIsNotNone(limit_reason(sample, 38))
        sample['temperatures'][0]['status'] = 0
        sample['status'] = 2
        self.assertIsNotNone(limit_reason(sample, 38))

    def test_ranges_and_battery_units(self):
        self.assertEqual(parse_battery('  temperature: 285\n  USB powered: true')['celsius'], 28.5)
        with self.assertRaises(ValueError):
            parse_battery('level: 80')
        self.assertEqual(summarize([]), {'samples': 0})
        samples = [dict(elapsed_seconds=15, thermal=dict(status=0, temperatures=[dict(type=3, celsius=28)]))]
        self.assertEqual(summarize(samples)['skin_celsius_range'], [28, 28])

    def test_missing_sensor_is_not_invented_as_zero(self):
        actual = parse_thermal('''Thermal Status: 0
Current temperatures from HAL:
 Temperature{mValue=NaN, mType=3, mName=skin, mStatus=0}
 Temperature{mValue=31.0, mType=0, mName=CPU0, mStatus=0}
Current cooling devices from HAL:
''')
        report = summarize([dict(elapsed_seconds=0, thermal=actual)])
        self.assertNotIn('skin_celsius_range', report)
        self.assertEqual(report['cpu_celsius_range'], [31, 31])


if __name__ == '__main__':
    unittest.main()
