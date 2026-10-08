"""Macro timeline window; all changes remain local until explicitly saved."""
from pathlib import Path
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from keyboard_layout import label
from keymap_model import KeymapError
from header_source import choose_header
from macro_model import Frame, MacroDocument, SLOT_COUNT, is_macro_code


class FrameDialog:
    def __init__(self, owner, frame, apply):
        self.owner, self.apply_callback = owner, apply
        self.window = tk.Toplevel(owner.window)
        self.window.title("编辑宏帧")
        self.window.transient(owner.window)
        self.window.resizable(False, False)
        body = ttk.Frame(self.window, padding=20)
        body.pack(fill="both", expand=True)
        ttk.Label(body, text="该帧表示此时仍按住的全部键；未列出的键会松开。", wraplength=700).grid(row=0, column=0, columnspan=2, sticky="w", pady=(0, 12))
        self.time = tk.StringVar(value=str(frame.at_ms))
        ttk.Label(body, text="名义时间（毫秒）").grid(row=1, column=0, sticky="w")
        ttk.Entry(body, textvariable=self.time, width=20).grid(row=1, column=1, sticky="w")
        self.choices = {f"{label(code)}  ·  {code}": code for code in owner.document.codes if is_macro_code(code)}
        inverse = {code: name for name, code in self.choices.items()}
        values = ["（无）", *sorted(self.choices)]
        self.keys = []
        for index in range(8):
            var = tk.StringVar(value=inverse[frame.keys[index]] if index < len(frame.keys) else "（无）")
            self.keys.append(var)
            cell = ttk.Frame(body)
            cell.grid(row=2 + index // 2, column=index % 2, padx=(0, 12), pady=6, sticky="w")
            ttk.Label(cell, text=f"键 {index + 1}").pack(anchor="w")
            ttk.Combobox(cell, textvariable=var, values=values, state="readonly", width=37).pack()
        actions = ttk.Frame(body)
        actions.grid(row=6, column=0, columnspan=2, sticky="e", pady=(16, 0))
        ttk.Button(actions, text="释放所有键", command=self.clear).pack(side="left", padx=5)
        ttk.Button(actions, text="取消", command=self.close).pack(side="left", padx=5)
        ttk.Button(actions, text="应用此帧", style="Accent.TButton", command=self.submit).pack(side="left", padx=5)
        self.window.protocol("WM_DELETE_WINDOW", self.close)
        self.window.bind("<Escape>", lambda _: self.close())
        self.window.grab_set()

    def clear(self):
        for variable in self.keys:
            variable.set("（无）")

    def submit(self):
        try:
            text = self.time.get().strip()
            if not text.isascii() or not text.isdecimal():
                raise KeymapError("时间必须是非负整数毫秒。")
            frame = Frame(int(text), tuple(self.choices[var.get()] for var in self.keys if var.get() != "（无）"))
            self.apply_callback(frame)
        except (KeymapError, ValueError) as exc:
            messagebox.showerror("无法应用此帧", str(exc), parent=self.window)
            return
        self.close()

    def close(self):
        self.window.destroy()
        self.owner.window.grab_set()


class MacroEditor:
    def __init__(self, parent, path, codes, slot=0, header_path=None):
        self.document = MacroDocument(path, codes)
        self.header_choices = {self.document.path: header_path} if header_path else {}
        self.window = tk.Toplevel(parent)
        self.window.title("宏编辑器")
        self.window.geometry("1060x680")
        self.window.minsize(900, 580)
        self.window.transient(parent)
        self.slot = tk.StringVar(value=str(slot))
        self.status = tk.StringVar()
        self.path_text = tk.StringVar(value=str(self.document.path))
        self.summary = tk.StringVar()
        self.build_ui()
        self.refresh()
        self.window.protocol("WM_DELETE_WINDOW", self.close)
        self.window.bind("<Control-s>", lambda _: self.save())
        self.window.bind("<Control-z>", lambda _: self.undo())
        self.window.grab_set()

    def build_ui(self):
        body = ttk.Frame(self.window, padding=20)
        body.pack(fill="both", expand=True)
        top = ttk.Frame(body)
        top.pack(fill="x")
        ttk.Label(top, text="宏动作与时间", font=("Microsoft YaHei UI", 20, "bold")).pack(side="left")
        self.save_button = ttk.Button(top, text="保存 macro_config.c", style="Accent.TButton", command=self.save)
        self.save_button.pack(side="right")
        ttk.Button(top, text="重新加载", command=self.reload).pack(side="right", padx=8)
        ttk.Button(top, text="打开宏文件…", command=self.open_file).pack(side="right")
        ttk.Label(body, textvariable=self.path_text, wraplength=980).pack(anchor="w", pady=(12, 8))
        slots = ttk.Frame(body)
        slots.pack(fill="x", pady=(0, 10))
        ttk.Label(slots, text="宏槽位").pack(side="left", padx=(0, 8))
        combo = ttk.Combobox(slots, textvariable=self.slot, values=[str(i) for i in range(SLOT_COUNT)], state="readonly", width=5)
        combo.pack(side="left")
        combo.bind("<<ComboboxSelected>>", lambda _: self.refresh())
        ttk.Label(slots, textvariable=self.summary).pack(side="left", padx=16)
        table_frame = ttk.Frame(body)
        table_frame.pack(fill="both", expand=True)
        self.table = ttk.Treeview(table_frame, columns=("time", "gap", "keys", "change"), show="headings", selectmode="browse")
        for name, title, width in [("time", "时间 / ms", 95), ("gap", "间隔 / ms", 95), ("keys", "该帧持有的全部键", 340), ("change", "相对上一帧", 350)]:
            self.table.heading(name, text=title)
            self.table.column(name, width=width, minwidth=70, stretch=name in ("keys", "change"))
        scrollbar = ttk.Scrollbar(table_frame, command=self.table.yview)
        self.table.configure(yscrollcommand=scrollbar.set)
        scrollbar.pack(side="right", fill="y")
        self.table.pack(fill="both", expand=True)
        self.table.bind("<Double-1>", lambda _: self.edit_frame())
        buttons = ttk.Frame(body)
        buttons.pack(fill="x", pady=12)
        for text, command in [("新增帧", self.add_frame), ("编辑帧", self.edit_frame), ("复制帧", self.duplicate_frame),
                              ("删除帧", self.delete_frame), ("末尾添加释放", self.append_release),
                              ("清空此宏", self.clear_slot), ("撤销", self.undo)]:
            ttk.Button(buttons, text=text, command=command).pack(side="left", padx=(0, 6))
        ttk.Label(body, text="每帧是完整按键状态，最多 8 键；每个宏最多 128 帧，时间递增，末帧必须释放。\n"
                  "时间为名义偏移，相邻时间差是固件最小间隔，实际执行可能更长。BLE 合并超过 6 个普通键会取消宏。\n"
                  "宏动作在本窗口单独保存；触发键在主窗口绑定“宏 0～3”或“取消宏”，再保存键位。",
                  wraplength=990, foreground="#67768d").pack(anchor="w")
        ttk.Label(body, textvariable=self.status, wraplength=990, foreground="#315ee7").pack(anchor="w", pady=(12, 0))

    def refresh(self, selected=None):
        slot = int(self.slot.get())
        frames = self.document.frames[slot]
        self.table.delete(*self.table.get_children())
        previous, previous_time = set(), 0
        for index, frame in enumerate(frames):
            keys = set(frame.keys)
            changes = []
            if keys - previous:
                changes.append("按下 " + " + ".join(label(code) for code in frame.keys if code not in previous))
            if previous - keys:
                changes.append("松开 " + " + ".join(label(code) for code in sorted(previous - keys)))
            self.table.insert("", "end", iid=str(index), values=(frame.at_ms, frame.at_ms - previous_time,
                              " + ".join(label(code) for code in frame.keys) or "释放所有键", "；".join(changes) or "保持"))
            previous, previous_time = keys, frame.at_ms
        if selected is not None and selected < len(frames):
            self.table.selection_set(str(selected))
            self.table.see(str(selected))
        count = len(self.document.changes)
        self.summary.set(f"宏 {slot} · {len(frames)} 帧 · {frames[-1].at_ms if frames else 0} ms" if frames else f"宏 {slot} · 未配置（不执行）")
        self.window.title(("* " if count else "") + "宏编辑器")
        self.save_button.state(["!disabled" if count else "disabled"])
        issue = "，末尾尚未释放，请添加释放帧后保存" if frames and frames[-1].keys else ""
        self.status.set((f"{count} 个宏有未保存修改" if count else "全部已保存") + issue)

    def selection(self):
        selection = self.table.selection()
        return int(selection[0]) if selection else None

    def apply_frame(self, frame, index=None):
        slot = int(self.slot.get())
        frames = self.document.frames[slot].copy()
        if index is None:
            frames.append(frame)
        else:
            frames[index] = frame
        frames.sort(key=lambda item: item.at_ms)
        self.document.update(slot, frames)
        self.refresh(frames.index(frame))

    def add_frame(self):
        frames = self.document.frames[int(self.slot.get())]
        frame = Frame(frames[-1].at_ms + 80 if frames else 0)
        FrameDialog(self, frame, self.apply_frame)

    def edit_frame(self):
        index = self.selection()
        if index is not None:
            frame = self.document.frames[int(self.slot.get())][index]
            FrameDialog(self, frame, lambda value: self.apply_frame(value, index))

    def duplicate_frame(self):
        index = self.selection()
        frames = self.document.frames[int(self.slot.get())]
        if index is not None:
            FrameDialog(self, Frame(frames[-1].at_ms + 80, frames[index].keys), self.apply_frame)

    def delete_frame(self):
        index = self.selection()
        if index is not None:
            slot = int(self.slot.get())
            frames = self.document.frames[slot].copy()
            del frames[index]
            self.document.update(slot, frames)
            self.refresh(min(index, len(frames) - 1) if frames else None)

    def append_release(self):
        frames = self.document.frames[int(self.slot.get())]
        try:
            self.apply_frame(Frame(frames[-1].at_ms + 80 if frames else 0))
        except KeymapError as exc:
            messagebox.showerror("无法添加释放帧", str(exc), parent=self.window)

    def clear_slot(self):
        slot = int(self.slot.get())
        if self.document.frames[slot] and messagebox.askyesno("清空宏", f"清空宏 {slot} 的全部帧？保存后此槽位将不执行。", parent=self.window):
            self.document.update(slot, [])
            self.refresh()

    def undo(self):
        self.document.undo()
        self.refresh()

    def save(self):
        try:
            backup = self.document.save()
        except (OSError, UnicodeError, KeymapError) as exc:
            messagebox.showerror("宏保存失败", str(exc), parent=self.window)
            return False
        self.refresh()
        if backup:
            self.status.set(f"宏已保存，备份：{backup.name}。请按文件路径确认是否需要同步到固件源码。")
        return True

    def may_discard(self):
        if not self.document.changes:
            return True
        answer = messagebox.askyesnocancel("宏未保存", "是否先保存宏动作的修改？", parent=self.window)
        return self.save() if answer is True else answer is False

    def load(self, path):
        try:
            path = Path(path).resolve()
            result = choose_header(path, self.window, self.header_choices.get(path))
            if result is None:
                return
            header, codes = result
            document = MacroDocument(path, codes)
        except (OSError, UnicodeError, KeymapError) as exc:
            messagebox.showerror("宏读取失败", str(exc), parent=self.window)
            return
        self.document = document
        self.header_choices[path] = header
        self.path_text.set(str(document.path))
        self.refresh()

    def reload(self):
        if self.may_discard():
            self.load(self.document.path)

    def open_file(self):
        if self.may_discard():
            path = filedialog.askopenfilename(parent=self.window, title="打开 macro_config.c", initialdir=self.document.path.parent,
                                              filetypes=[("C 源文件", "*.c")])
            if path:
                self.load(path)

    def close(self):
        if not self.may_discard():
            return False
        self.window.destroy()
        return True
