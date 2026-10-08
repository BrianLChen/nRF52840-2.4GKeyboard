from pathlib import Path
import re
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from keyboard_layout import KEYS, KEY_BY_INDEX
from keymap_model import FN, KeymapDocument, KeymapError, read_codes

CONFIG = Path(__file__).resolve().parents[2]


class DocumentTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "keymap.c"
        self.original = (CONFIG / "keymap.c").read_bytes()
        self.path.write_bytes(self.original)
        self.doc = KeymapDocument(self.path)

    def test_existing_file_roundtrip_and_layout(self):
        self.assertEqual([len(row) for row in self.doc.values], [85, 85])
        self.assertEqual(self.doc.render(), self.original)
        self.assertEqual(len(KEYS), 85)
        self.assertEqual(sorted(KEY_BY_INDEX), list(range(85)))
        self.assertEqual(self.doc.values[0][6], "KEYBOARD_ESCAPE")
        self.assertEqual(self.doc.values[0][84], "CONSUMER_MUTE")
        self.assertGreater(KEY_BY_INDEX[84].x, KEY_BY_INDEX[76].x)
        self.assertLess(KEY_BY_INDEX[84].y, KEY_BY_INDEX[83].y)

    def test_only_selected_layer_token_changes(self):
        self.doc.assign(1, 8, "KEYBOARD_ESCAPE")
        first = self.original.index(b"KEYBOARD_Q,")
        second = self.original.index(b"KEYBOARD_Q,", first + 1)
        expected = self.original[:second] + b"KEYBOARD_ESCAPE" + self.original[second + len(b"KEYBOARD_Q"):]
        self.assertEqual(self.doc.render(), expected)
        self.assertEqual(self.path.read_bytes(), self.original)

    def test_macro_trigger_roundtrip_preserves_rotation(self):
        codes = read_codes(CONFIG / "keymap.h")
        for code in ("KBD_KEY_MACRO_0", "KBD_KEY_MACRO_1", "KBD_KEY_MACRO_2",
                     "KBD_KEY_MACRO_3", "KBD_KEY_MACRO_CANCEL"):
            self.assertIn(code, codes)
        self.assertEqual(self.doc.values[0][47], "KEYBOARD_F6")
        self.assertEqual(self.doc.values[1][47], "KBD_KEY_MACRO_0")
        self.assertEqual(self.doc.values[1][48], "KBD_KEY_MACRO_CANCEL")
        rotations = [row.copy() for row in self.doc.knob_values]
        self.doc.assign(1, 47, "KBD_KEY_MACRO_2")
        self.doc.save()
        reopened = KeymapDocument(self.path)
        self.assertEqual(reopened.values[1][47], "KBD_KEY_MACRO_2")
        self.assertEqual(reopened.values[0][47], "KEYBOARD_F6")
        self.assertEqual(reopened.knob_values, rotations)
        with self.assertRaises(KeymapError):
            reopened.assign_knob(0, "KBD_KNOB_CW", "KBD_KEY_MACRO_0")

    def test_save_and_backup(self):
        self.doc.assign(0, 84, "CONSUMER_PLAY_PAUSE")
        expected = self.doc.render()
        backup = self.doc.save()
        self.assertEqual(backup.read_bytes(), self.original)
        self.assertEqual(self.path.read_bytes(), expected)
        self.assertFalse(self.doc.changes)
        self.assertEqual(KeymapDocument(self.path).values[0][84], "CONSUMER_PLAY_PAUSE")
        self.assertEqual(list(self.path.parent.glob("*.tmp")), [])

    def test_external_modification_is_preserved(self):
        self.doc.assign(0, 8, "KEYBOARD_ESCAPE")
        external = self.original + b"\n/* external edit */\n"
        self.path.write_bytes(external)
        with self.assertRaises(KeymapError):
            self.doc.save()
        self.assertEqual(self.path.read_bytes(), external)
        self.assertEqual(list(self.path.parent.glob("*.bak")), [])

    def test_fn_move_swaps_both_layers_and_undoes_together(self):
        before = [row.copy() for row in self.doc.values]
        # P is normal on layer 0, pairing on layer 1; preserve both on swap.
        self.doc.assign(0, 57, FN)
        self.assertEqual([row[57] for row in self.doc.values], [FN, FN])
        self.assertEqual([row[54] for row in self.doc.values], ["KEYBOARD_P", "KBD_KEY_PAIRING"])
        self.assertEqual(len(self.doc.changes), 4)
        self.doc.undo()
        self.assertEqual(self.doc.values, before)

    def test_fn_removal_applies_to_both_layers(self):
        self.doc.assign(1, 54, "MODIFIER_RIGHT_ALT")
        self.assertTrue(all(row[54] == "MODIFIER_RIGHT_ALT" for row in self.doc.values))
        self.assertFalse(any(FN in row for row in self.doc.values))

    def test_crlf_bom_comments_preserved(self):
        source = self.original.decode("utf-8-sig").replace("\r\n", "\n").replace("\n", "\r\n")
        source = "\ufeff/* key_table[x][y] = { fake }; */\r\n" + source
        self.path.write_bytes(source.encode("utf-8"))
        doc = KeymapDocument(self.path)
        doc.assign(0, 6, "KEYBOARD_F1")
        self.assertEqual(doc.render(), source.replace("KEYBOARD_ESCAPE", "KEYBOARD_F1", 1).encode("utf-8"))

    def test_rejects_unsupported_expression_without_writing(self):
        malformed = self.original.replace(b"KEYBOARD_Q", b"(KEYBOARD_Q + 1)", 1)
        self.path.write_bytes(malformed)
        with self.assertRaises(KeymapError):
            KeymapDocument(self.path)
        self.assertEqual(self.path.read_bytes(), malformed)

    def test_rejects_truncated_initializer(self):
        self.path.write_bytes(self.original[:self.original.index(b"KEYBOARD_ESCAPE")])
        with self.assertRaises(KeymapError):
            KeymapDocument(self.path)

    def test_preserves_extra_explicit_entries(self):
        extended = self.original.replace(b"CONSUMER_MUTE,", b"CONSUMER_MUTE, 0, 0, 0,")
        self.path.write_bytes(extended)
        doc = KeymapDocument(self.path)
        self.assertEqual([len(row) for row in doc.values], [88, 88])
        doc.assign(0, 6, "KEYBOARD_F1")
        self.assertEqual(doc.render(), extended.replace(b"KEYBOARD_ESCAPE", b"KEYBOARD_F1", 1))

    def test_candidates_are_from_firmware_header(self):
        codes = read_codes(CONFIG / "keymap.h")
        self.assertIn("KBD_KEY_PAIRING", codes)
        self.assertIn("KBD_KEY_NONE", codes)
        self.assertIn("CONSUMER_VOLUME_INCREASE", codes)
        self.assertNotIn("KBD_KEYMAP_LAYER_COUNT", codes)
        self.assertTrue(all(code in codes for row in self.doc.values for code in row))

    def test_unchanged_save_creates_no_backup(self):
        self.assertIsNone(self.doc.save())
        self.assertEqual(list(self.path.parent.glob("*.bak")), [])

    def test_knob_roundtrip_and_only_selected_direction_changes(self):
        self.assertEqual(self.doc.knob_values, [
            {"KBD_KNOB_CW": "CONSUMER_VOLUME_INCREASE", "KBD_KNOB_CCW": "CONSUMER_VOLUME_DECREASE"},
            {"KBD_KNOB_CW": "CONSUMER_VOLUME_INCREASE", "KBD_KNOB_CCW": "CONSUMER_VOLUME_DECREASE"},
        ])
        self.doc.assign_knob(1, "KBD_KNOB_CCW", "CONSUMER_PREVIOUS_TRACK")
        token = b"[KBD_KNOB_CCW] = CONSUMER_VOLUME_DECREASE"
        first = self.original.index(token)
        second = self.original.index(token, first + 1)
        expected = self.original[:second] + self.original[second:].replace(token, b"[KBD_KNOB_CCW] = CONSUMER_PREVIOUS_TRACK", 1)
        self.assertEqual(self.doc.render(), expected)
        self.assertEqual(self.doc.key_changes, [])
        self.assertEqual(self.doc.changes, [(1, "KBD_KNOB_CCW")])

    def test_knob_save_and_backup_with_key_edit(self):
        self.doc.assign_knob(0, "KBD_KNOB_CW", "KBD_KEY_NONE")
        self.doc.assign_knob(1, "KBD_KNOB_CW", "CONSUMER_NEXT_TRACK")
        self.doc.assign(0, 84, "CONSUMER_PLAY_PAUSE")
        expected = self.doc.render()
        backup = self.doc.save()
        self.assertEqual(backup.read_bytes(), self.original)
        self.assertEqual(self.path.read_bytes(), expected)
        self.assertFalse(self.doc.changes)
        reopened = KeymapDocument(self.path)
        self.assertEqual(reopened.knob_values[0]["KBD_KNOB_CW"], "KBD_KEY_NONE")
        self.assertEqual(reopened.knob_values[1]["KBD_KNOB_CW"], "CONSUMER_NEXT_TRACK")
        self.assertEqual(reopened.values[0][84], "CONSUMER_PLAY_PAUSE")

    def test_mixed_undo_includes_knob_and_fn_changes(self):
        self.doc.assign_knob(1, "KBD_KNOB_CW", "CONSUMER_NEXT_TRACK")
        rotation_only = self.doc.render()
        self.doc.assign(0, 57, FN)
        self.doc.assign_knob(0, "KBD_KNOB_CCW", "KBD_KEY_NONE")
        self.doc.undo()
        self.assertEqual(self.doc.knob_values[0]["KBD_KNOB_CCW"], "CONSUMER_VOLUME_DECREASE")
        self.doc.undo()
        self.assertEqual(self.doc.render(), rotation_only)
        self.doc.undo()
        self.assertEqual(self.doc.render(), self.original)
        self.assertFalse(self.doc.changes)

    def test_knob_rejects_unsupported_actions(self):
        for code in ("KEYBOARD_A", "MODIFIER_LEFT_CTRL", FN, "KBD_KEY_PAIRING", "CONSUMER_MUTE + 1"):
            with self.subTest(code=code), self.assertRaises(KeymapError):
                self.doc.assign_knob(0, "KBD_KNOB_CW", code)
        self.assertFalse(self.doc.changes)
        self.assertFalse(self.doc.undo_stack)

    def test_knob_only_change_external_conflict(self):
        self.doc.assign_knob(0, "KBD_KNOB_CW", "KBD_KEY_NONE")
        external = self.original + b"\n/* external */\n"
        self.path.write_bytes(external)
        with self.assertRaises(KeymapError):
            self.doc.save()
        self.assertEqual(self.path.read_bytes(), external)

    def test_knob_designators_can_be_reordered(self):
        block = ("static const uint16_t knob_map[2][2] = {\n"
                 "[1] = {[KBD_KNOB_CCW] = CONSUMER_PREVIOUS_TRACK, /* CW */\n"
                 "[KBD_KNOB_CW] = CONSUMER_NEXT_TRACK},\n"
                 "[0] = {[KBD_KNOB_CCW] = KBD_KEY_NONE, [KBD_KNOB_CW] = CONSUMER_MUTE}};")
        source = re.sub(r"static const uint16_t knob_map.*?\n};", lambda _: block,
                        self.original.decode("utf-8"), count=1, flags=re.S)
        self.path.write_bytes(source.encode("utf-8"))
        doc = KeymapDocument(self.path)
        self.assertEqual(doc.knob_values[1]["KBD_KNOB_CW"], "CONSUMER_NEXT_TRACK")
        doc.assign_knob(0, "KBD_KNOB_CCW", "CONSUMER_VOLUME_DECREASE")
        self.assertEqual(doc.render(), source.replace("[KBD_KNOB_CCW] = KBD_KEY_NONE",
                         "[KBD_KNOB_CCW] = CONSUMER_VOLUME_DECREASE", 1).encode("utf-8"))

    def test_legacy_keymap_without_rotation_still_edits_keys(self):
        source = re.sub(rb"static const uint16_t knob_map.*?\n};", b"", self.original, count=1, flags=re.S)
        source = re.sub(rb"uint16_t keymap_get_knob_code.*?\n}", b"", source, count=1, flags=re.S)
        self.path.write_bytes(source)
        doc = KeymapDocument(self.path)
        self.assertEqual(doc.knob_values, [])
        doc.assign(0, 6, "KEYBOARD_F1")
        self.assertEqual(doc.render(), source.replace(b"KEYBOARD_ESCAPE", b"KEYBOARD_F1", 1))
        with self.assertRaises(KeymapError):
            doc.assign_knob(0, "KBD_KNOB_CW", "KBD_KEY_NONE")

    def test_rejects_incomplete_or_ambiguous_knob_map(self):
        for before, after in [
            (b"[KBD_KNOB_CCW]", b"[KBD_KNOB_CW]"),
            (b"[1] = {", b"[0] = {"),
            (b"[KBD_KNOB_CW] = CONSUMER_VOLUME_INCREASE,", b""),
            (b"[KBD_KNOB_CW] = CONSUMER_VOLUME_INCREASE,", b"[KBD_KNOB_CW] = CONSUMER_VOLUME_INCREASE + 1,"),
        ]:
            with self.subTest(after=after):
                source = self.original.replace(before, after, 1)
                self.path.write_bytes(source)
                with self.assertRaises(KeymapError):
                    KeymapDocument(self.path)
                self.assertEqual(self.path.read_bytes(), source)


if __name__ == "__main__":
    unittest.main()
