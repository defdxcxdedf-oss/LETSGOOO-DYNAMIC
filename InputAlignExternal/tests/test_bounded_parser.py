import sys
import unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'worker/vendor'))
from gdgen import levelstring

class BoundedParserTests(unittest.TestCase):
    def test_exact_limit_and_empty_segments(self):
        self.assertEqual(levelstring.parse_level('kA2,0;;1,1;;1,8;',max_objects=2),({'kA2':'0'},[{'1':'1'},{'1':'8'}]))

    def test_excess_stops_before_next_dictionary(self):
        with patch.object(levelstring,'parse_object',wraps=levelstring.parse_object) as parse:
            with self.assertRaises(ValueError):
                levelstring.parse_level('kA2,0;1,1;1,8;1,9;',max_objects=2)
            self.assertEqual(parse.call_count,3) # header + two accepted objects
