from pathlib import Path
import sys
import tempfile
import tkinter as tk
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from header_source import choose_header
from keyboard_config import DEFAULT_KEYMAP, KeyboardEditor


class HeaderSourceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name).resolve()
        self.source = self.folder / "src" / "keymap.c"
        self.source.parent.mkdir()
        self.source.write_bytes(DEFAULT_KEYMAP.read_bytes())
        self.header = self.folder / "headers" / "keymap.h"
        self.header.parent.mkdir()
        self.header.write_bytes(DEFAULT_KEYMAP.with_suffix(".h").read_bytes())
        self.root = tk.Tk()
        self.root.withdraw()
        self.addCleanup(self.root.destroy)

    def make_app(self):
        with patch("header_source.filedialog.askopenfilename", return_value=str(self.header)):
            return KeyboardEditor(self.root, self.source)

    def test_automatic_src_inc_and_include(self):
        for directory in ("inc", "include"):
            with self.subTest(directory=directory):
                header = self.folder / directory / "keymap.h"
                header.parent.mkdir()
                header.write_bytes(self.header.read_bytes())
                with patch("header_source.filedialog.askopenfilename") as picker:
                    path, codes = choose_header(self.source, self.root)
                    self.assertEqual(path, header)
                    self.assertIn("KEYBOARD_A", codes)
                    picker.assert_not_called()
                header.unlink()

    def test_manual_header_is_remembered_for_reload(self):
        app = self.make_app()
        self.assertEqual(app.header_path, self.header)
        self.assertIn(str(self.header), app.file_text.get())
        with patch("header_source.filedialog.askopenfilename") as picker:
            app.reload()
            picker.assert_not_called()
        self.assertEqual(app.path, self.source)

    def test_change_header_preserves_unsaved_key_edits(self):
        app = self.make_app()
        app.document.assign(0, 8, "KEYBOARD_ESCAPE")
        document = app.document
        other = self.folder / "other.h"
        other.write_text("enum { KEYBOARD_A = 20, KEYBOARD_B = 21 };", encoding="utf-8")
        with patch("keyboard_config.filedialog.askopenfilename", return_value=str(other)):
            app.change_header()
        self.assertIs(app.document, document)
        self.assertEqual(app.document.values[0][8], "KEYBOARD_ESCAPE")
        self.assertEqual(app.codes, ["KEYBOARD_A", "KEYBOARD_B"])
        self.assertEqual(app.header_path, other)
        self.assertEqual(self.source.read_bytes(), DEFAULT_KEYMAP.read_bytes())

    def test_cancel_new_header_keeps_current_document(self):
        app = self.make_app()
        document = app.document
        other = self.folder / "other_project" / "keymap.c"
        other.parent.mkdir()
        other.write_bytes(self.source.read_bytes())
        with patch("header_source.filedialog.askopenfilename", return_value=""):
            app.load(other)
        self.assertIs(app.document, document)
        self.assertEqual(app.header_path, self.header)
        self.assertEqual(app.path, self.source)

    def test_invalid_header_keeps_current_selection(self):
        app = self.make_app()
        other = self.folder / "invalid.h"
        other.write_text("/* no key definitions */", encoding="utf-8")
        with patch("keyboard_config.filedialog.askopenfilename", return_value=str(other)), patch("keyboard_config.messagebox.showerror") as error:
            app.change_header()
            error.assert_called_once()
        self.assertEqual(app.header_path, self.header)
        self.assertIn("KEYBOARD_A", app.codes)

    def test_macro_reload_uses_separately_selected_header(self):
        app = self.make_app()
        macro = self.source.with_name("macro_config.c")
        macro.write_bytes(DEFAULT_KEYMAP.with_name("macro_config.c").read_bytes())
        app.open_macros()
        editor = app.macro_editor
        editor.window.withdraw()
        with patch("header_source.filedialog.askopenfilename") as picker:
            editor.reload()
            picker.assert_not_called()
        self.assertEqual(editor.header_choices[macro], self.header)
        self.assertIn("KEYBOARD_A", editor.document.codes)


if __name__ == "__main__":
    unittest.main()
