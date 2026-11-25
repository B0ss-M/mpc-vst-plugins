#!/usr/bin/env python3
"""gen_vst.knob_fx(): vst.json "knob_fx", or an effect's sample_page banks, -> plugin.json's knobs for the MPC Sample's Knob FX."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_vst  # noqa: E402

P = [{"key": "gain", "name": "Gain"}, {"key": "bass", "name": "Bass"}, {"key": "treble", "name": "Treble"},
     {"key": "status", "name": "Status", "display": "string"}, {"key": "tap", "name": "Tap", "momentary": True},
     {"key": "mix", "name": "Mix"}]
PAGE = {"banks": [{"name": "AMP", "knobs": ["gain", "bass", "treble"]}, {"name": "OUT", "knobs": ["mix"]}, {"name": "X", "knobs": ["bass"]}]}


class T(unittest.TestCase):
    def test_from_the_sample_page_banks(self):
        k = gen_vst.knob_fx({"effect": True, "sample_page": PAGE}, P)
        self.assertEqual(k["knobs"], [{"parameter": 0, "label": "Gain"}, {"parameter": 1, "label": "Bass"}, {"parameter": 2, "label": "Treble"}])
        self.assertEqual(k["shiftKnobs"], [{"parameter": 5, "label": "Mix"}])   # the second bank; a third has nowhere to go

    def test_one_bank_leaves_shift_unassigned(self):
        k = gen_vst.knob_fx({"effect": True, "sample_page": {"banks": [{"knobs": ["gain", "", "mix"]}]}}, P)
        self.assertEqual(k, {"knobs": [{"parameter": 0, "label": "Gain"}, {}, {"parameter": 5, "label": "Mix"}]})

    def test_explicit_mapping_wins(self):
        k = gen_vst.knob_fx({"effect": True, "sample_page": PAGE,
                             "knob_fx": {"knobs": [{"key": "mix", "label": "Blend"}, None, "gain"], "short_name": "Amp"}}, P)
        self.assertEqual(k, {"knobs": [{"parameter": 5, "label": "Blend"}, {}, {"parameter": 0, "label": "Gain"}], "shortName": "Amp"})

    def test_none_without_a_page_or_for_an_instrument(self):
        self.assertIsNone(gen_vst.knob_fx({"effect": True}, P))
        self.assertIsNone(gen_vst.knob_fx({"sample_page": PAGE}, P))
        # an explicit empty list is kept: the Sample then leaves those knobs unassigned instead of putting params 0-2 there
        self.assertEqual(gen_vst.knob_fx({"effect": True, "knob_fx": {"knobs": []}}, P), {"knobs": []})

    def test_refusals(self):
        for kf in ({"knobs": ["nope"]}, {"knobs": ["status"]}, {"knobs": ["tap"]}, {"knobs": ["gain"] * 4},
                   {"knobs": [{"label": "x"}]}, {"short_name": "much too long name"}, {"knob": ["gain"]}):
            with self.assertRaises(SystemExit, msg=kf):
                gen_vst.knob_fx({"effect": True, "knob_fx": kf}, P)


if __name__ == "__main__":
    unittest.main()
