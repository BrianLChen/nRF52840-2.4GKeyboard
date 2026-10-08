from pathlib import Path
import sys
import tempfile
import tkinter as tk
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from keymap_model import KeymapError, read_codes
from macro_model import Frame, MacroDocument, validate_frames
from keyboard_config import KeyboardEditor
from macro_editor import FrameDialog

CONFIG = Path(__file__).resolve().parents[2]


class MacroTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "macro_config.c"
        self.original = (CONFIG / "macro_config.c").read_bytes()
        self.path.write_bytes(self.original)
        self.codes = read_codes(CONFIG / "keymap.h")
        self.doc = MacroDocument(self.path, self.codes)

    def test_original_demo_loads_without_changes(self):
        self.assertEqual([frame.at_ms for frame in self.doc.frames[0]], [0, 80, 160, 260, 400, 480])
        self.assertEqual(self.doc.frames[0][1].keys, ("KEYBOARD_A", "KEYBOARD_B"))
        self.assertEqual(self.doc.frames[1:], [[], [], []])
        self.assertEqual(self.doc.render(), self.original)
        self.assertIsNone(self.doc.save())
        self.assertFalse(list(self.path.parent.glob("*.bak")))

    def test_edit_existing_and_add_all_slots_roundtrip(self):
        for slot in range(4):
            self.doc.update(slot, [Frame(slot * 10, ("MODIFIER_LEFT_CTRL", "KEYBOARD_C")), Frame(100 + slot * 10)])
        expected = [row.copy() for row in self.doc.frames]
        backup = self.doc.save()
        self.assertEqual(backup.read_bytes(), self.original)
        loaded = MacroDocument(self.path, self.codes)
        self.assertEqual(loaded.frames, expected)
        self.assertEqual(self.doc.changes, [])
        self.assertIn(b"return id < KBD_MACRO_SLOT_COUNT ? &programs[id] : NULL;", self.path.read_bytes())

    def test_disabling_all_slots_can_be_reenabled(self):
        self.doc.update(0, [])
        self.doc.save()
        self.assertEqual(self.doc.frames, [[], [], [], []])
        self.assertIn(b"[0] = { 0 }", self.path.read_bytes())
        self.doc.update(0, [Frame(0, ("KEYBOARD_Z",)), Frame(40)])
        self.doc.save()
        self.assertEqual(MacroDocument(self.path, self.codes).frames[0][0].keys, ("KEYBOARD_Z",))

    def test_saving_one_slot_preserves_other_array_and_functions(self):
        self.doc.update(1, [Frame(0, ("KEYBOARD_Z",)), Frame(50)])
        result = self.doc.render().decode("utf-8")
        start, end = self.doc.array_spans["demo_frames"]
        self.assertIn(self.doc.source[start:end], result)
        self.assertTrue(result.endswith(self.doc.source[self.doc.program_span[1]:]))

    def test_invalid_timing_counts_and_codes_rejected(self):
        invalid = [
            [Frame(0), Frame(0)], [Frame(1), Frame(0)], [Frame(-1)], [Frame(2**32)],
            [Frame(0, ("KEYBOARD_A",) * 9)], [Frame(i) for i in range(129)],
        ]
        invalid += [[Frame(0, (code,)), Frame(30)] for code in (
            "CONSUMER_MUTE", "KBD_KEY_FN", "KBD_KEY_MACRO_0", "KBD_KEY_PAIRING", "KEYBOARD_RESERVED", "KEYBOARD_ERROR_ROLL_OVER", "UNKNOWN")]
        for frames in invalid:
            with self.subTest(frames=frames), self.assertRaises(KeymapError):
                self.doc.update(1, frames)
        self.assertFalse(self.doc.changes)

    def test_firmware_limits_and_delayed_first_frame(self):
        frames = [Frame(100 + i, ("MODIFIER_LEFT_CTRL",) * 8) for i in range(127)] + [Frame(227)]
        validate_frames(frames, self.codes)
        self.doc.update(1, frames)
        self.doc.save()
        self.assertEqual(len(self.doc.frames[1]), 128)

    def test_incomplete_macro_can_be_edited_but_not_saved(self):
        self.doc.update(1, [Frame(0, ("KEYBOARD_A",))])
        with self.assertRaisesRegex(KeymapError, "最后一帧"):
            self.doc.save()
        self.assertEqual(self.path.read_bytes(), self.original)
        self.assertFalse(list(self.path.parent.glob("*.bak")))
        self.doc.update(1, [Frame(0, ("KEYBOARD_A",)), Frame(80)])
        self.doc.save()

    def test_cross_slot_undo(self):
        self.doc.update(1, [Frame(0)])
        self.doc.update(2, [Frame(20)])
        self.doc.undo()
        self.assertEqual(self.doc.changes, [1])
        self.doc.undo()
        self.assertEqual(self.doc.render(), self.original)

    def test_external_changes_block_save(self):
        self.doc.update(1, [Frame(0)])
        external = self.original + b"\n/* external */\n"
        self.path.write_bytes(external)
        with self.assertRaises(KeymapError):
            self.doc.save()
        self.assertEqual(self.path.read_bytes(), external)

    def test_numeric_frame_count_is_updated(self):
        source = self.original.replace(b"sizeof(demo_frames) / sizeof(demo_frames[0])", b"6")
        self.path.write_bytes(source)
        doc = MacroDocument(self.path, self.codes)
        doc.update(0, [Frame(0)])
        doc.save()
        self.assertEqual(MacroDocument(self.path, self.codes).frames[0], [Frame(0)])

    def test_bom_crlf_and_no_edit_roundtrip(self):
        source = b"\xef\xbb\xbf" + self.original.decode("utf-8-sig").replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8")
        self.path.write_bytes(source)
        doc = MacroDocument(self.path, self.codes)
        self.assertEqual(doc.render(), source)
        doc.update(1, [Frame(0)])
        result = doc.render()
        self.assertTrue(result.startswith(b"\xef\xbb\xbf"))
        self.assertNotIn(b"\n", result.replace(b"\r\n", b""))

    def test_rejects_ambiguous_or_unsupported_config(self):
        for before, after in [
            (b".key_count = 1", b".key_count = 2"),
            (b".at_ms =   0", b".at_ms = DELAY(0)"),
            (b".frames = demo_frames", b".frames = missing_frames"),
            (b"KEYBOARD_A }", b"CONSUMER_MUTE }"),
            (b"demo_frames[]", b"demo_frames[6]"),
            (b"programs[KBD_MACRO_SLOT_COUNT]", b"programs[2]"),
        ]:
            with self.subTest(after=after):
                source = self.original.replace(before, after, 1)
                self.path.write_bytes(source)
                with self.assertRaises(KeymapError):
                    MacroDocument(self.path, self.codes)
                self.assertEqual(self.path.read_bytes(), source)


class MacroGuiTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        for name in ("keymap.c", "keymap.h", "macro_config.c"):
            (self.folder / name).write_bytes((CONFIG / name).read_bytes())
        self.root = tk.Tk()
        self.root.withdraw()
        self.addCleanup(self.root.destroy)
        self.app = KeyboardEditor(self.root, self.folder / "keymap.c")
        self.app.open_macros()
        self.editor = self.app.macro_editor
        self.editor.window.withdraw()

    def test_main_macro_filter_and_rotation_exclusion(self):
        self.app.group.set("宏")
        self.app.filter_codes()
        self.assertEqual(len(self.app.tree.get_children()), 5)
        self.app.select_target(84, "KBD_KNOB_CW")
        self.assertNotIn("KBD_KEY_MACRO_0", self.app.tree.get_children())

    def test_create_sequence_save_and_reload(self):
        editor = self.editor
        self.assertEqual(len(editor.table.get_children()), 6)
        editor.slot.set("1")
        editor.refresh()
        editor.apply_frame(Frame(40, ("MODIFIER_LEFT_CTRL", "KEYBOARD_C")))
        with patch("macro_editor.messagebox.showerror") as error:
            self.assertFalse(editor.save())
            error.assert_called_once()
        editor.append_release()
        self.assertTrue(editor.save())
        editor.reload()
        self.assertEqual(editor.document.frames[1], [Frame(40, ("MODIFIER_LEFT_CTRL", "KEYBOARD_C")), Frame(120)])
        self.assertTrue(editor.save_button.instate(["disabled"]))
        self.assertEqual((self.folder / "keymap.c").read_bytes(), (CONFIG / "keymap.c").read_bytes())

    def test_frame_dialog_applies_modifier_and_release(self):
        editor = self.editor
        editor.slot.set("2")
        dialog = FrameDialog(editor, Frame(0), editor.apply_frame)
        dialog.window.withdraw()
        code = next(text for text, code in dialog.choices.items() if code == "MODIFIER_LEFT_CTRL")
        dialog.keys[0].set(code)
        dialog.submit()
        self.assertEqual(editor.document.frames[2][0].keys, ("MODIFIER_LEFT_CTRL",))
        editor.append_release()
        editor.undo()
        with patch("macro_editor.messagebox.askyesnocancel", return_value=None):
            self.assertFalse(editor.close())
        self.assertTrue(editor.window.winfo_exists())


if __name__ == "__main__":
    unittest.main()
