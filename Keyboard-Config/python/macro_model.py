"""Restricted parser/editor for the firmware's static macro configuration."""
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
import os
import re
import tempfile

from keymap_model import KeymapError

SLOT_COUNT, MAX_FRAMES, KEYS_PER_FRAME = 4, 128, 8
UINT32_MAX = 0xffffffff


def is_macro_code(code):
    return code.startswith("MODIFIER_") or (code.startswith("KEYBOARD_") and code not in {
        "KEYBOARD_RESERVED", "KEYBOARD_ERROR_ROLL_OVER", "KEYBOARD_POST_FAIL", "KEYBOARD_ERROR_UNDEFINED"})


@dataclass(frozen=True)
class Frame:
    at_ms: int
    keys: tuple = ()


def validate_frames(frames, codes, final=True):
    if len(frames) > MAX_FRAMES:
        raise KeymapError("每个宏最多 128 帧。")
    previous = -1
    for index, frame in enumerate(frames):
        if not isinstance(frame.at_ms, int) or not 0 <= frame.at_ms <= UINT32_MAX:
            raise KeymapError(f"第 {index + 1} 帧时间必须是 0～{UINT32_MAX} 的整数毫秒。")
        if frame.at_ms <= previous:
            raise KeymapError("各帧时间必须严格递增；同时按下的键应放在同一帧。")
        previous = frame.at_ms
        if len(frame.keys) > KEYS_PER_FRAME:
            raise KeymapError(f"第 {index + 1} 帧最多包含 8 个键。")
        if any(key not in codes or not is_macro_code(key) for key in frame.keys):
            raise KeymapError(f"第 {index + 1} 帧含不支持的键；宏仅支持普通键和修饰键。")
    if final and frames and frames[-1].keys:
        raise KeymapError("最后一帧必须释放所有键。请添加一个空帧，再保存。")


def number(text):
    match = re.fullmatch(r"(0[xX][0-9a-fA-F]+|0|[1-9][0-9]*)[uUlL]*", text.strip())
    if not match:
        raise KeymapError(f"宏配置需要整数常量，不支持表达式：{text.strip()}")
    return int(match[1], 16 if match[1].lower().startswith("0x") else 10)


def split_items(text):
    """Split at commas outside nested C braces, parentheses and brackets."""
    stack, result, start = [], [], 0
    pairs = {"}": "{", ")": "(", "]": "["}
    for index, char in enumerate(text):
        if char in "{([":
            stack.append(char)
        elif char in "})]":
            if not stack or stack.pop() != pairs[char]:
                raise KeymapError("宏配置括号不匹配。")
        elif char == "," and not stack:
            if not text[start:index].strip():
                raise KeymapError("宏配置包含空的初始化项。")
            result.append(text[start:index].strip())
            start = index + 1
    if stack:
        raise KeymapError("宏配置不完整。")
    if text[start:].strip():
        result.append(text[start:].strip())
    return result


def fields(text):
    if not text.startswith("{") or not text.endswith("}"):
        raise KeymapError("宏配置需要结构体初始化。")
    result = {}
    for item in split_items(text[1:-1]):
        match = re.fullmatch(r"\.([a-z_]+)\s*=\s*(.+)", item, re.S)
        if not match or match[1] in result:
            raise KeymapError("宏配置包含重复字段或不支持的初始化格式。")
        result[match[1]] = match[2].strip()
    return result


def initializers(masked, kind):
    pattern = rf"\bstatic\s+const\s+struct\s+{kind}\s+([A-Za-z_]\w*)\s*\[([^\]]*)\]\s*=\s*\{{"
    result = {}
    for match in re.finditer(pattern, masked):
        dimension = match[2].strip()
        if kind == "kbd_macro_frame" and dimension:
            raise KeymapError("宏帧数组请使用 [] 自动确定长度，避免增删帧后固定数组长度不匹配。")
        if kind == "kbd_macro_program" and dimension != "KBD_MACRO_SLOT_COUNT":
            if not dimension or number(dimension) != SLOT_COUNT:
                raise KeymapError("programs 数组长度必须是 KBD_MACRO_SLOT_COUNT 或 4。")
        start, depth, end = match.end() - 1, 1, match.end()
        while end < len(masked) and depth:
            depth += (masked[end] == "{") - (masked[end] == "}")
            end += 1
        if depth or not re.match(r"\s*;", masked[end:]):
            raise KeymapError("宏配置数组不完整。")
        if match[1] in result:
            raise KeymapError("宏配置数组名称重复。")
        result[match[1]] = (start, end)
    return result


class MacroDocument:
    def __init__(self, path, codes):
        self.path = Path(path).resolve()
        self.codes = set(codes)
        self.original_bytes = self.path.read_bytes()
        self.source = self.original_bytes.decode("utf-8")
        self.newline = "\r\n" if "\r\n" in self.source else "\n"
        masked = re.sub(r"/\*.*?\*/|//[^\r\n]*", lambda m: " " * len(m[0]), self.source, flags=re.S)
        self.array_spans = initializers(masked, "kbd_macro_frame")
        self.arrays = {}
        for name, (start, end) in self.array_spans.items():
            frames = []
            for item in split_items(masked[start + 1:end - 1]):
                entry = fields(item)
                if not {"at_ms", "key_count"} <= entry.keys() or entry.keys() - {"at_ms", "key_count", "keys"}:
                    raise KeymapError("宏帧必须包含 at_ms、key_count，可选 keys。")
                keys_text = entry.get("keys", "{}")
                if not keys_text.startswith("{") or not keys_text.endswith("}"):
                    raise KeymapError("宏 keys 必须是键值列表。")
                keys = tuple(split_items(keys_text[1:-1]))
                if number(entry["key_count"]) != len(keys):
                    raise KeymapError("宏 key_count 与显式键值数量不一致。")
                frames.append(Frame(number(entry["at_ms"]), keys))
            validate_frames(frames, self.codes, final=False)
            self.arrays[name] = frames
        programs = initializers(masked, "kbd_macro_program")
        if set(programs) != {"programs"}:
            raise KeymapError("找不到唯一的 programs 宏槽位表。")
        self.program_span = programs["programs"]
        self.names = [None] * SLOT_COUNT
        self.literal_counts = set()
        self.program_start = masked.rfind("static", 0, self.program_span[0])
        seen = set()
        start, end = self.program_span
        for item in split_items(masked[start + 1:end - 1]):
            match = re.fullmatch(r"\[\s*([0-3])\s*\]\s*=\s*(\{.*\})", item, re.S)
            if not match or int(match[1]) in seen:
                raise KeymapError("programs 需要唯一的 [0]～[3] 槽位下标。")
            slot = int(match[1])
            seen.add(slot)
            if re.fullmatch(r"\{\s*0\s*\}", match[2]):
                continue
            entry = fields(match[2])
            if not entry:
                continue
            if set(entry) != {"frames", "frame_count"}:
                raise KeymapError("宏槽位必须定义 frames 和 frame_count。")
            name = entry["frames"]
            count = re.sub(r"\s+", "", entry["frame_count"])
            if name in ("NULL", "0") and count == "0":
                continue
            if name not in self.arrays:
                raise KeymapError(f"找不到宏帧数组：{name}")
            if count != f"sizeof({name})/sizeof({name}[0])":
                if number(count) != len(self.arrays[name]):
                    raise KeymapError("宏 frame_count 与数组帧数不一致。")
                self.literal_counts.add(slot)
            if name in self.names:
                raise KeymapError("多个槽位共享同一个帧数组，请先为每个槽位使用独立数组。")
            self.names[slot] = name
        self.original = [list(self.arrays[name]) if name else [] for name in self.names]
        self.frames = [row.copy() for row in self.original]
        self.undo_stack = []

    @property
    def changes(self):
        return [slot for slot in range(SLOT_COUNT) if self.frames[slot] != self.original[slot]]

    def update(self, slot, frames):
        if slot not in range(SLOT_COUNT):
            raise KeymapError("宏槽位必须是 0～3。")
        validate_frames(frames, self.codes, final=False)
        if self.frames[slot] != frames:
            self.undo_stack.append([row.copy() for row in self.frames])
            self.frames[slot] = list(frames)

    def undo(self):
        if self.undo_stack:
            self.frames = self.undo_stack.pop()

    def render_frames(self, frames):
        rows = ["{"]
        for frame in frames:
            keys = ", .keys = { " + ", ".join(frame.keys) + " }" if frame.keys else ""
            rows.append(f"\t{{ .at_ms = {frame.at_ms}, .key_count = {len(frame.keys)}{keys} }},")
        rows.append("}")
        return self.newline.join(rows)

    def render(self):
        if not self.changes:
            return self.original_bytes
        for slot, frames in enumerate(self.frames):
            try:
                validate_frames(frames, self.codes)
            except KeymapError as exc:
                raise KeymapError(f"宏 {slot}：{exc}") from exc
        edits, additions = [], []
        names = self.names.copy()
        for slot in self.changes:
            frames, name = self.frames[slot], names[slot]
            if not frames:
                names[slot] = None  # Keep the old unreferenced array and its comments.
                continue
            if name:
                edits.append((*self.array_spans[name], self.render_frames(frames)))
            else:
                name = f"config_macro_{slot}_frames"
                while re.search(rf"\b{re.escape(name)}\b", self.source):
                    name += "_new"
                names[slot] = name
                additions.append(f"static const struct kbd_macro_frame {name}[] = {self.render_frames(frames)};{self.newline}{self.newline}")
        if names != self.names or self.literal_counts.intersection(self.changes):
            rows = ["{"]
            for slot, name in enumerate(names):
                if name:
                    rows.append(f"\t[{slot}] = {{ .frames = {name}, .frame_count = sizeof({name}) / sizeof({name}[0]) }},")
            if len(rows) == 1:
                rows.append("\t[0] = { 0 },")
            rows.append("}")
            edits.append((*self.program_span, self.newline.join(rows)))
        if additions:
            # Insert before the programs declaration, after all existing arrays.
            start = self.program_start
            edits.append((start, start, "".join(additions)))
        result = self.source
        for start, end, replacement in sorted(edits, reverse=True):
            result = result[:start] + replacement + result[end:]
        return result.encode("utf-8")

    def save(self):
        if self.path.read_bytes() != self.original_bytes:
            raise KeymapError("宏文件已被其他程序修改，请重新加载后编辑。")
        if not self.changes:
            return None
        output = self.render()
        backup = self.path.with_name(self.path.name + "." + datetime.now().strftime("%Y%m%d-%H%M%S-%f") + ".bak")
        with backup.open("xb") as stream:
            stream.write(self.original_bytes)
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=self.path.parent, prefix=".macro-", suffix=".tmp", delete=False) as stream:
                temporary = Path(stream.name)
                stream.write(output)
                stream.flush()
                os.fsync(stream.fileno())
            if self.path.read_bytes() != self.original_bytes:
                raise KeymapError("保存期间宏文件被外部修改，请重新加载。")
            os.replace(temporary, self.path)
        finally:
            if temporary is not None and temporary.exists():
                temporary.unlink()
        self.__init__(self.path, self.codes)
        return backup
