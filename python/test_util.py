#!/usr/bin/env python3
import sys
import unittest
from pathlib import Path
from datetime import datetime, timedelta
from collections import defaultdict

# Add parent path to import util
package_root = Path(__file__).resolve().parents[2]
for path_entry in [str(package_root), str(Path(__file__).resolve().parent)]:
    if path_entry not in sys.path:
        sys.path.insert(0, path_entry)

try:
    from util import get_disruption_status, parse_args, packet_tree
except ImportError:
    from python.util import get_disruption_status, parse_args, packet_tree


class TestUtilDisruptionWarning(unittest.TestCase):

    def test_get_disruption_status_no_data(self):
        label, style, elapsed = get_disruption_status(None, datetime.now())
        self.assertEqual(label, 'NO DATA')
        self.assertEqual(style, 'dim')
        self.assertEqual(elapsed, 0.0)

    def test_get_disruption_status_ok(self):
        now = datetime.now()
        recent_time = now - timedelta(seconds=0.5)
        label, style, elapsed = get_disruption_status(recent_time, now, stale_timeout=2.0, lost_timeout=5.0)
        self.assertIn('0.5s', label)
        self.assertNotIn('STALE', label)
        self.assertNotIn('LOST', label)
        self.assertEqual(style, 'default')

    def test_get_disruption_status_stale(self):
        now = datetime.now()
        stale_time = now - timedelta(seconds=3.0)
        label, style, elapsed = get_disruption_status(stale_time, now, stale_timeout=2.0, lost_timeout=5.0)
        self.assertIn('STALE', label)
        self.assertIn('3.0s', label)
        self.assertEqual(style, 'yellow bold')

    def test_get_disruption_status_lost(self):
        now = datetime.now()
        lost_time = now - timedelta(seconds=8.0)
        label, style, elapsed = get_disruption_status(lost_time, now, stale_timeout=2.0, lost_timeout=5.0)
        self.assertIn('LOST', label)
        self.assertIn('8.0s', label)
        self.assertEqual(style, 'red bold')

    def test_packet_tree_detects_lost_components(self):
        all_packets = defaultdict(lambda: [defaultdict(lambda: [defaultdict(lambda: [[], None, -1]), None]), None])
        now = datetime.now()
        lost_time = now - timedelta(seconds=10.0)

        unit_id = 0x01
        comp_id = 0x10
        pkt_id = ord('A')

        all_packets[unit_id][1] = lost_time
        all_packets[unit_id][0][comp_id][1] = lost_time
        all_packets[unit_id][0][comp_id][0][pkt_id][1] = lost_time

        panel, lost_comps = packet_tree(all_packets, [unit_id, comp_id, pkt_id], stale_timeout=2.0, lost_timeout=5.0)
        self.assertTrue(len(lost_comps) > 0)
        self.assertIn('Unit 0x1 Comp 0x10', lost_comps[0])


if __name__ == '__main__':
    unittest.main()
