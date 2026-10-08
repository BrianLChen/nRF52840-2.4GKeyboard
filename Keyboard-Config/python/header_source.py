"""Resolve separated C/header layouts without borrowing another project's codes."""
from pathlib import Path
from tkinter import filedialog

from keymap_model import KeymapError, read_codes


def read_header(path):
    header = Path(path).resolve()
    codes = read_codes(header)
    if not codes:
        raise KeymapError(f"{header} 中没有可用的键值定义。")
    return header, codes


def choose_header(source, parent, preferred=None):
    source = Path(source).resolve()
    candidates = ([Path(preferred)] if preferred is not None else []) + [
        source.with_name("keymap.h"),
        source.parent.parent / "inc" / "keymap.h",
        source.parent.parent / "include" / "keymap.h",
    ]
    for path in candidates:
        if path.is_file():
            return read_header(path)
    selected = filedialog.askopenfilename(
        parent=parent, title=f"为 {source.name} 选择配套的 keymap.h",
        initialdir=source.parent, filetypes=[("C 头文件", "*.h"), ("所有文件", "*.*")])
    return read_header(selected) if selected else None
