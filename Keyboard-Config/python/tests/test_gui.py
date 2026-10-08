"""Exercise Tk callbacks with a hidden window and temporary configuration files."""
from pathlib import Path
import re
import sys
import tempfile
import tkinter as tk
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from keyboard_config import DEFAULT_KEYMAP, KeyboardEditor
from keymap_model import KeymapDocument, is_knob_code


class RotationGuiTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "keymap.c"
        self.path.write_bytes(DEFAULT_KEYMAP.read_bytes())
        self.path.with_suffix(".h").write_bytes(DEFAULT_KEYMAP.with_suffix(".h").read_bytes())
        self.root = tk.Tk()
        self.root.withdraw()
        self.addCleanup(self.root.destroy)
        self.app = KeyboardEditor(self.root, self.path)
        self.root.update_idletasks()

    def test_rotation_filters_candidates_and_press_restores_all_codes(self):
        app = self.app
        app.group.set("普通按键")
        app.search.set("Escape")
        app.knob_buttons["KBD_KNOB_CW"].invoke()
        candidates = app.tree.get_children()
        self.assertTrue(candidates)
        self.assertTrue(all(is_knob_code(code) for code in candidates))
        self.assertIn("KBD_KEY_NONE", candidates)
        self.assertEqual(app.selected_text.get(), "旋钮 · 顺时针 ↻")
        app.knob_buttons[None].invoke()
        self.assertIsNone(app.knob_direction)
        self.assertEqual(app.selected, 84)
        self.assertIn("KEYBOARD_ESCAPE", app.tree.get_children())
        self.assertIn("KBD_KEY_FN", app.tree.get_children())

    def test_rotation_edit_layer_switch_undo_save_and_reload(self):
        app = self.app
        app.knob_buttons["KBD_KNOB_CW"].invoke()
        app.layer.set(1)
        app.refresh()
        app.tree.selection_set("CONSUMER_NEXT_TRACK")
        app.apply()
        self.assertEqual(app.code_text.get(), "CONSUMER_NEXT_TRACK")
        self.assertIn("*", app.knob_buttons["KBD_KNOB_CW"].cget("text"))
        self.assertFalse(app.save_button.instate(["disabled"]))
        app.layer.set(0)
        app.refresh()
        self.assertEqual(app.code_text.get(), "CONSUMER_VOLUME_INCREASE")
        app.undo()
        self.assertFalse(app.document.changes)
        app.knob_buttons["KBD_KNOB_CCW"].invoke()
        app.tree.selection_set("KBD_KEY_NONE")
        app.apply()
        self.assertTrue(app.save())
        self.assertTrue(app.save_button.instate(["disabled"]))
        app.reload()
        self.assertEqual(app.code_text.get(), "KBD_KEY_NONE")
        self.assertEqual(KeymapDocument(self.path).knob_values[0]["KBD_KNOB_CCW"], "KBD_KEY_NONE")
        self.assertEqual(len(list(self.path.parent.glob("*.bak"))), 1)

    def test_opening_legacy_file_disables_rotation_and_clears_selection(self):
        app = self.app
        app.knob_buttons["KBD_KNOB_CW"].invoke()
        source = self.path.read_bytes()
        source = re.sub(rb"static const uint16_t knob_map.*?\n};", b"", source, count=1, flags=re.S)
        source = re.sub(rb"uint16_t keymap_get_knob_code.*?\n}", b"", source, count=1, flags=re.S)
        self.path.write_bytes(source)
        app.reload()
        self.assertIsNone(app.knob_direction)
        self.assertTrue(app.knob_buttons["KBD_KNOB_CW"].instate(["disabled"]))
        self.assertTrue(app.knob_buttons["KBD_KNOB_CCW"].instate(["disabled"]))
        self.assertFalse(app.knob_buttons[None].instate(["disabled"]))
        self.assertIn("KEYBOARD_ESCAPE", app.tree.get_children())


if __name__ == "__main__":
    unittest.main()
