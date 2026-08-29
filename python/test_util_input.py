import unittest
from unittest.mock import patch

from . import util


@unittest.skipUnless(util.sys.platform == 'win32', 'Windows key input test')
class TestWindowsKeyInput(unittest.TestCase):
    def test_no_key_returns_empty_string(self):
        with patch.object(util.msvcrt, 'kbhit', return_value=False):
            self.assertEqual(util.getkey(), '')

    def test_printable_key_is_returned_without_quotes(self):
        with patch.object(util.msvcrt, 'kbhit', return_value=True), \
             patch.object(util.msvcrt, 'getwch', return_value='q'):
            self.assertEqual(util.getkey(), 'q')
            self.assertEqual(util.on_input(util.getkey(), {}, [0, 0, 0], None), 'quit')


if __name__ == '__main__':
    unittest.main()
