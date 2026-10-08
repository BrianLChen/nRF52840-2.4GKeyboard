"""A standard-library-only, two-layer keyboard map editor."""
from pathlib import Path
import argparse
import math
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from keyboard_layout import KEYS, KEY_BY_INDEX, category, label
from keymap_model import FN, KNOB_DIRECTIONS, KeymapDocument, KeymapError, is_knob_code
from header_source import choose_header, read_header


HERE = Path(__file__).resolve().parent
DEFAULT_KEYMAP = HERE.parent / "keymap.c"
BG = "#f3f5f9"
INK = "#18263b"
MUTED = "#67768d"
BLUE = "#315ee7"
KNOB_LABELS = {"KBD_KNOB_CW": "顺时针 ↻", "KBD_KNOB_CCW": "逆时针 ↺"}


class KeyboardEditor:
    def __init__(self, root, path=DEFAULT_KEYMAP):
        self.root = root
        self.document = None
        self.macro_editor = None
        self.header_path = None
        self.header_choices = {}
        self.path = Path(path)
        self.selected = 6
        self.knob_direction = None
        self.layer = tk.IntVar(value=0)
        self.search = tk.StringVar()
        self.group = tk.StringVar(value="全部")
        self.status = tk.StringVar(value="选择一个按键，然后在右侧选择新功能。")
        self.selected_text = tk.StringVar()
        self.current_text = tk.StringVar()
        self.code_text = tk.StringVar()
        self.change_text = tk.StringVar()
        self.file_text = tk.StringVar()
        self.pending_text = tk.StringVar(value="在下方选择新功能")
        self.codes = []
        root.title("Keyboard Config · 键位编辑器")
        root.geometry("1440x820")
        root.minsize(1100, 720)
        root.configure(bg=BG)
        style = ttk.Style(root)
        style.theme_use("clam")
        style.configure("TFrame", background=BG)
        style.configure("Card.TFrame", background="white")
        style.configure("TLabel", background=BG, foreground=INK, font=("Microsoft YaHei UI", 10))
        style.configure("Card.TLabel", background="white")
        style.configure("Muted.TLabel", foreground=MUTED)
        style.configure("TButton", font=("Microsoft YaHei UI", 10), padding=(12, 8))
        style.configure("Accent.TButton", background=BLUE, foreground="white", borderwidth=0)
        style.map("Accent.TButton", background=[("active", "#244bc7"), ("disabled", "#acb7d4")])
        style.configure("TRadiobutton", background=BG, font=("Microsoft YaHei UI", 10), padding=8)
        style.configure("Treeview", rowheight=30, font=("Microsoft YaHei UI", 10), background="white", fieldbackground="white", borderwidth=0)
        style.configure("Treeview.Heading", font=("Microsoft YaHei UI", 9), padding=6)
        style.map("Treeview", background=[("selected", "#e3ebff")], foreground=[("selected", INK)])
        self.build_ui()
        self.search.trace_add("write", lambda *_: self.filter_codes())
        root.bind("<Control-s>", lambda _: self.save())
        root.bind("<Control-z>", lambda _: self.undo())
        root.protocol("WM_DELETE_WINDOW", self.close)
        self.load(self.path)

    def build_ui(self):
        outer = ttk.Frame(self.root, padding=24)
        outer.pack(fill="both", expand=True)
        header = ttk.Frame(outer)
        header.pack(fill="x")
        titles = ttk.Frame(header)
        titles.pack(side="left")
        ttk.Label(titles, text="键位编辑器", font=("Microsoft YaHei UI", 23, "bold")).pack(anchor="w")
        ttk.Label(titles, text="84 个按键 + 旋钮按压与旋转  /  双层映射", style="Muted.TLabel").pack(anchor="w", pady=(4, 0))
        self.save_button = ttk.Button(header, text="保存到 keymap.c", style="Accent.TButton", command=self.save)
        self.save_button.pack(side="right", padx=(10, 0))
        ttk.Button(header, text="重新加载", command=self.reload).pack(side="right", padx=(10, 0))
        ttk.Button(header, text="打开文件…", command=self.open_file).pack(side="right")
        ttk.Button(header, text="宏编辑器…", command=self.open_macros).pack(side="right", padx=(0, 10))
        file_row = ttk.Frame(outer)
        file_row.pack(fill="x", pady=(18, 12))
        ttk.Button(file_row, text="选择头文件…", command=self.change_header).pack(side="right", padx=(10, 0))
        ttk.Label(file_row, textvariable=self.file_text, style="Muted.TLabel", wraplength=900).pack(side="left", anchor="w")

        body = ttk.Frame(outer)
        body.pack(fill="both", expand=True)
        body.columnconfigure(0, weight=1)
        body.columnconfigure(1, minsize=300)
        body.rowconfigure(0, weight=1)
        left = ttk.Frame(body)
        left.grid(row=0, column=0, sticky="nsew", padx=(0, 20))
        toolbar = ttk.Frame(left)
        toolbar.pack(fill="x", pady=(0, 12))
        for value, text in [(0, "普通层"), (1, "Fn 层")]:
            ttk.Radiobutton(toolbar, text=text, value=value, variable=self.layer, command=self.refresh).pack(side="left")
        ttk.Label(toolbar, textvariable=self.change_text, foreground=BLUE).pack(side="right")
        self.canvas = tk.Canvas(left, background="#e8edf5", highlightthickness=0, height=420)
        self.canvas.pack(fill="both", expand=True)
        self.canvas.bind("<Configure>", self.draw_keyboard)
        self.canvas.bind("<Button-1>", self.click_key)
        legend = ttk.Frame(left)
        legend.pack(fill="x", pady=(12, 14))
        for text, color in [("●  已选中", BLUE), ("●  已修改", "#c68623"), ("●  与普通层不同", "#268b88")]:
            ttk.Label(legend, text=text, foreground=color).pack(side="left", padx=(0, 20))
        knob_row = ttk.Frame(left)
        knob_row.pack(fill="x", pady=(0, 12))
        ttk.Label(knob_row, text="旋钮", font=("Microsoft YaHei UI", 11, "bold")).pack(side="left", padx=(0, 12))
        self.knob_buttons = {}
        for direction in (None, *KNOB_DIRECTIONS):
            button = ttk.Button(knob_row, command=lambda d=direction: self.select_target(84, d))
            button.pack(side="left", fill="x", expand=True, padx=(0, 6))
            self.knob_buttons[direction] = button
        info = ttk.Frame(left, style="Card.TFrame", padding=16)
        info.pack(fill="x")
        ttk.Label(info, text="使用方式", style="Card.TLabel", font=("Microsoft YaHei UI", 11, "bold")).pack(anchor="w")
        self.help_label = ttk.Label(info, style="Card.TLabel", foreground=MUTED,
            text="① 点击物理按键  →  ② 搜索并选择功能  →  ③ 应用到按键  →  ④ 保存\n"
                 "Fn 移位会交换两层对应键值；将 Fn 改为普通键时也会联动两层。\n"
                 "旋钮：点击圆形键设置按压，点击两侧弧形箭头设置旋转；支持媒体功能或禁用。\n"
                 "保存前自动备份。修改进入固件还需同步到 Lib/src/keymap.c，再编译烧录。",
            wraplength=760, justify="left")
        self.help_label.pack(anchor="w", pady=(8, 0))

        right = ttk.Frame(body, style="Card.TFrame", padding=16, width=310)
        right.grid(row=0, column=1, sticky="nsew")
        right.grid_propagate(False)
        right.columnconfigure(0, weight=1)
        right.rowconfigure(7, weight=1)
        ttk.Label(right, text="当前按键", style="Card.TLabel", foreground=MUTED).grid(row=0, column=0, sticky="w")
        ttk.Label(right, textvariable=self.selected_text, style="Card.TLabel", font=("Microsoft YaHei UI", 16, "bold")).grid(row=1, column=0, sticky="w", pady=(6, 4))
        ttk.Label(right, textvariable=self.current_text, style="Card.TLabel").grid(row=2, column=0, sticky="w")
        ttk.Label(right, textvariable=self.code_text, style="Card.TLabel", foreground=MUTED, wraplength=275).grid(row=3, column=0, sticky="w", pady=(4, 14))
        search_row = ttk.Frame(right, style="Card.TFrame")
        search_row.grid(row=4, column=0, sticky="ew")
        ttk.Label(search_row, text="搜索功能 / C 常量", style="Card.TLabel").pack(anchor="w", pady=(0, 5))
        self.search_entry = ttk.Entry(search_row, textvariable=self.search, font=("Microsoft YaHei UI", 10))
        self.search_entry.pack(fill="x")
        combo = ttk.Combobox(right, textvariable=self.group, state="readonly", values=["全部", "普通按键", "修饰键", "媒体", "宏", "内部功能"])
        combo.grid(row=5, column=0, sticky="ew", pady=8)
        combo.bind("<<ComboboxSelected>>", lambda _: self.filter_codes())
        ttk.Label(right, textvariable=self.pending_text, style="Card.TLabel", foreground=BLUE, wraplength=270).grid(row=6, column=0, sticky="w", pady=(0, 8))
        list_frame = ttk.Frame(right, style="Card.TFrame")
        list_frame.grid(row=7, column=0, sticky="nsew")
        self.tree = ttk.Treeview(list_frame, columns=("name", "group"), show="headings", selectmode="browse", height=7)
        self.tree.heading("name", text="功能")
        self.tree.heading("group", text="分类")
        self.tree.column("name", width=166, minwidth=100)
        self.tree.column("group", width=76, minwidth=65, stretch=False)
        scrollbar = ttk.Scrollbar(list_frame, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=scrollbar.set)
        scrollbar.pack(side="right", fill="y")
        self.tree.pack(side="left", fill="both", expand=True)
        self.tree.bind("<<TreeviewSelect>>", self.preview_candidate)
        self.tree.bind("<Double-1>", lambda _: self.apply())
        self.tree.bind("<Return>", lambda _: self.apply())
        self.apply_button = ttk.Button(right, text="应用到按键", style="Accent.TButton", command=self.apply)
        self.apply_button.grid(row=8, column=0, sticky="ew", pady=(12, 6))
        self.undo_button = ttk.Button(right, text="撤销上一步  Ctrl+Z", command=self.undo)
        self.undo_button.grid(row=9, column=0, sticky="ew")
        ttk.Label(outer, textvariable=self.status, style="Muted.TLabel", wraplength=1300).pack(anchor="w", pady=(16, 0))

    def load(self, path):
        try:
            document = KeymapDocument(path)
            result = choose_header(document.path, self.root, self.header_choices.get(document.path))
            if result is None:
                return
            header, codes = result
        except (OSError, UnicodeError, KeymapError) as exc:
            messagebox.showerror("读取失败", str(exc), parent=self.root)
            return
        self.document = document
        self.codes = codes
        self.header_path = header
        self.header_choices[document.path] = header
        if not document.knob_values:
            self.knob_direction = None
        self.path = document.path
        self.file_text.set(f"源文件：{self.path}\n头文件：{self.header_path}")
        self.filter_codes()
        self.refresh()
        self.status.set(f"已从所选头文件加载 {len(codes)} 个键值。点击键帽选择物理位置；右侧下标从 0 开始。")

    def change_header(self):
        if self.document is None:
            return
        path = filedialog.askopenfilename(parent=self.root, title="选择配套的 keymap.h",
            initialdir=self.header_path.parent if self.header_path else self.path.parent,
            filetypes=[("C 头文件", "*.h"), ("所有文件", "*.*")])
        if not path:
            return
        try:
            header, codes = read_header(path)
        except (OSError, UnicodeError, KeymapError) as exc:
            messagebox.showerror("头文件读取失败", str(exc), parent=self.root)
            return
        self.header_path, self.codes = header, codes
        self.header_choices[self.path] = header
        self.file_text.set(f"源文件：{self.path}\n头文件：{self.header_path}")
        self.filter_codes()
        self.refresh()
        self.status.set(f"已更新 {len(codes)} 个候选键值，当前键位修改已保留。")

    def refresh(self):
        if self.document is None:
            return
        layer = self.layer.get()
        if self.knob_direction is not None:
            code = self.document.knob_values[layer][self.knob_direction]
            self.selected_text.set("旋钮 · " + KNOB_LABELS[self.knob_direction])
            location = "旋转"
        else:
            code = self.document.values[layer][self.selected]
            self.selected_text.set(KEY_BY_INDEX[self.selected].legend)
            location = f"下标 {self.selected}"
        self.current_text.set(f"{'普通层' if layer == 0 else 'Fn 层'} · {location} · {label(code)}")
        self.code_text.set(code)
        self.apply_button.configure(text="应用到旋转方向" if self.knob_direction else "应用到按键")
        count = len(self.document.changes)
        self.change_text.set(f"{count} 项未保存" if count else "全部已保存")
        self.root.title(("* " if count else "") + "Keyboard Config · 键位编辑器")
        self.save_button.state(["!disabled" if count else "disabled"])
        self.undo_button.state(["!disabled" if self.document.undo_stack else "disabled"])
        for direction, button in self.knob_buttons.items():
            available = direction is None or bool(self.document.knob_values)
            name = KNOB_LABELS[direction] if direction else "按压"
            value = (self.document.knob_values[layer][direction] if direction else self.document.values[layer][84]) if available else None
            changed = (layer, direction if direction else 84) in self.document.changes
            button.configure(text=f"{name}：{label(value) if available else '未配置'}{' *' if changed else ''}",
                             style="Accent.TButton" if self.selected == 84 and self.knob_direction == direction else "TButton")
            button.state(["!disabled" if available else "disabled"])
        self.draw_keyboard()

    def draw_keyboard(self, event=None):
        if self.document is None:
            return
        c = self.canvas
        c.delete("all")
        width, height = c.winfo_width(), c.winfo_height()
        # Reserve space to the right for the clockwise arrow around the knob.
        unit = min((width - 24) / 17.8, (height - 24) / 6.5)
        if unit < 1:
            return
        ox, oy = (width - unit * 17.8) / 2, (height - unit * 6.5) / 2
        self.hitboxes = []
        layer = self.layer.get()
        changes = set(self.document.key_changes)
        for key in KEYS:
            x, y = ox + key.x * unit + 2, oy + key.y * unit + 2
            x2, y2 = ox + (key.x + key.width) * unit - 2, oy + (key.y + 1) * unit - 2
            self.hitboxes.append((x, y, x2, y2, key.index))
            code = self.document.values[layer][key.index]
            selected = key.index == self.selected and self.knob_direction is None
            changed = (layer, key.index) in changes
            other_layer = layer == 1 and code != self.document.values[0][key.index]
            fill = BLUE if selected else "#fff5df" if changed else "#e3f4f1" if other_layer else "#ffffff"
            outline = BLUE if selected else "#dab368" if changed else "#84bcb6" if other_layer else "#ccd5e2"
            shape = c.create_oval if key.knob else c.create_rectangle
            shape(x, y + 2, x2, y2 + 2, fill="#cdd6e3", outline="")
            shape(x, y, x2, y2, fill=fill, outline=outline, width=2 if selected else 1)
            font_size = max(7, min(12, int(unit * .19)))
            name = label(code)
            if code.startswith("MODIFIER_"):
                name = name.split(" ", 1)[-1]
            if name == "Backspace" and key.width < 1.5:
                name = "Bksp"
            c.create_text((x + x2) / 2, (y + y2) / 2 - 3, text=name, width=max(12, x2 - x - 5),
                          fill="white" if selected else INK, font=("Microsoft YaHei UI", font_size, "bold"))
            if key.knob or name != key.legend:
                c.create_text((x + x2) / 2, y2 - 7, text="按压" if key.knob else key.legend,
                              fill="#dbe5ff" if selected else MUTED, font=("Microsoft YaHei UI", max(6, font_size - 3)))
        self.draw_knob_arrows(ox, oy, unit)
        self.help_label.configure(wraplength=max(300, width - 40))

    def draw_knob_arrows(self, ox, oy, unit):
        """Two separate curved buttons; the centre remains the physical press."""
        knob = KEY_BY_INDEX[84]
        cx, cy = ox + (knob.x + .5) * unit, oy + (knob.y + .5) * unit
        c = self.canvas
        layer = self.layer.get()
        available = bool(self.document.knob_values)
        self.knob_arrow_centres = {}

        def points(radius, angles):
            return [coordinate for angle in angles
                    for coordinate in (cx + radius * unit * math.cos(math.radians(angle)),
                                       cy - radius * unit * math.sin(math.radians(angle)))]

        for direction, start, end in [("KBD_KNOB_CW", 82, 8), ("KBD_KNOB_CCW", 98, 172)]:
            angles = [start + (end - start) * i / 24 for i in range(25)]
            selected = self.knob_direction == direction
            changed = (layer, direction) in self.document.knob_changes
            other_layer = available and layer == 1 and (
                self.document.knob_values[1][direction] != self.document.knob_values[0][direction])
            fill = ("#dde3ec" if not available else BLUE if selected else
                    "#fff5df" if changed else "#e3f4f1" if other_layer else "#ffffff")
            outline = ("#ccd5e2" if not available else BLUE if selected else
                       "#dab368" if changed else "#84bcb6" if other_layer else "#ccd5e2")
            tags = ("knob_arrow", direction)
            c.create_polygon(points(.94, angles) + points(.60, reversed(angles)),
                             fill=fill, outline=outline, width=2 if selected else 1, tags=tags)
            # A real curved arrow avoids font-dependent clockwise/CCW glyphs.
            arrow_angles = [start + (end - start) * (.14 + .72 * i / 24) for i in range(25)]
            c.create_line(points(.77, arrow_angles), smooth=True,
                          fill=MUTED if not available else "white" if selected else INK,
                          width=max(1.5, unit * .035), arrow=tk.LAST,
                          arrowshape=(unit * .13, unit * .16, unit * .075), tags=tags)
            self.knob_arrow_centres[direction] = tuple(points(.77, [(start + end) / 2]))

    def click_key(self, event):
        # Hit the actual curved button rather than its enclosing rectangle.
        for item in reversed(self.canvas.find_overlapping(event.x, event.y, event.x, event.y)):
            tags = self.canvas.gettags(item)
            if "knob_arrow" in tags:
                self.select_target(84, next(direction for direction in KNOB_DIRECTIONS if direction in tags))
                return
        for x, y, x2, y2, index in getattr(self, "hitboxes", []):
            if x <= event.x <= x2 and y <= event.y <= y2:
                self.select_target(index)
                break

    def select_target(self, index, direction=None):
        if self.document is None or (direction is not None and not self.document.knob_values):
            return
        switching_kind = (direction is None) != (self.knob_direction is None)
        self.selected = index
        self.knob_direction = direction
        if switching_kind:
            self.group.set("全部")
            self.search.set("")
        self.filter_codes()
        self.refresh()

    def filter_codes(self):
        self.tree.delete(*self.tree.get_children())
        query = self.search.get().strip().casefold()
        for code in self.codes:
            if self.knob_direction is not None and not is_knob_code(code):
                continue
            if self.group.get() not in ("全部", category(code)):
                continue
            if query and query not in f"{label(code)} {code} {category(code)}".casefold():
                continue
            self.tree.insert("", "end", iid=code, values=(label(code), category(code)))
        self.pending_text.set("在下方选择新功能")
        self.apply_button.state(["disabled"])

    def preview_candidate(self, event=None):
        selection = self.tree.selection()
        self.pending_text.set("准备设置为：" + label(selection[0]) if selection else "在下方选择新功能")
        self.apply_button.state(["!disabled" if selection and self.document else "disabled"])

    def apply(self):
        selection = self.tree.selection()
        if self.document is None or not selection:
            return
        try:
            if self.knob_direction is not None:
                self.document.assign_knob(self.layer.get(), self.knob_direction, selection[0])
            else:
                self.document.assign(self.layer.get(), self.selected, selection[0])
        except KeymapError as exc:
            messagebox.showerror("无法应用", str(exc), parent=self.root)
            return
        self.refresh()
        self.status.set("已应用到编辑区，点击保存后才写入文件。可用 Ctrl+Z 撤销。")

    def undo(self):
        if self.document:
            self.document.undo()
            self.refresh()
            self.status.set("已撤销上一步。")

    def save(self):
        if self.document is None or not self.document.changes:
            return True
        if not any(FN in row for row in self.document.values):
            if not messagebox.askyesno("没有 Fn 键", "当前配置没有 Fn 键，将无法通过按键进入 Fn 层。仍然保存？", parent=self.root):
                return False
        try:
            backup = self.document.save()
        except (OSError, UnicodeError, KeymapError) as exc:
            messagebox.showerror("保存失败", str(exc), parent=self.root)
            return False
        self.refresh()
        self.status.set(f"保存成功。备份：{backup.name}。固件源文件尚未同步。")
        return True

    def may_discard(self):
        if self.document is None or not self.document.changes:
            return True
        answer = messagebox.askyesnocancel("未保存的修改", "是否先保存修改？\n选择“否”将放弃编辑区的修改。", parent=self.root)
        return self.save() if answer is True else answer is False

    def reload(self):
        if self.may_discard():
            self.load(self.path)

    def open_file(self):
        if not self.may_discard():
            return
        path = filedialog.askopenfilename(parent=self.root, title="打开 keymap.c", initialdir=self.path.parent,
                                          filetypes=[("C 源文件", "*.c"), ("所有文件", "*.*")])
        if path:
            self.load(path)

    def open_macros(self):
        from macro_editor import MacroEditor
        if self.document is None:
            return
        if self.macro_editor is not None and self.macro_editor.window.winfo_exists():
            self.macro_editor.window.lift()
            return
        path = self.path.with_name("macro_config.c")
        if not path.exists():
            selected = filedialog.askopenfilename(parent=self.root, title="选择 macro_config.c", initialdir=self.path.parent,
                                                 filetypes=[("C 源文件", "*.c")])
            if not selected:
                return
            path = Path(selected)
        code = self.document.values[self.layer.get()][self.selected]
        slot = int(code[-1]) if code in [f"KBD_KEY_MACRO_{i}" for i in range(4)] else 0
        try:
            self.macro_editor = MacroEditor(self.root, path, self.codes, slot, header_path=self.header_path)
        except (OSError, UnicodeError, KeymapError) as exc:
            messagebox.showerror("宏读取失败", str(exc), parent=self.root)

    def close(self):
        if self.macro_editor is not None and self.macro_editor.window.winfo_exists():
            if not self.macro_editor.close():
                return
        if self.may_discard():
            self.root.destroy()


def main():
    parser = argparse.ArgumentParser(description="Tkinter 键位编辑器，无第三方依赖")
    parser.add_argument("--keymap", type=Path, default=DEFAULT_KEYMAP)
    args = parser.parse_args()
    root = tk.Tk()
    try:
        KeyboardEditor(root, args.keymap)
    except (OSError, UnicodeError, KeymapError) as exc:
        messagebox.showerror("启动失败", str(exc), parent=root)
        root.destroy()
        return
    root.mainloop()


if __name__ == "__main__":
    main()
