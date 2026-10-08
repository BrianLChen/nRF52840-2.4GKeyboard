"""Read a deliberately small C initializer grammar; never execute source code."""
from datetime import datetime
from pathlib import Path
import os
import re
import tempfile


FN = "KBD_KEY_FN"
KNOB_DIRECTIONS = ("KBD_KNOB_CW", "KBD_KNOB_CCW")
TOKEN = re.compile(r"\s+|/\*.*?\*/|//[^\r\n]*|[A-Za-z_]\w*|0[xX][0-9a-fA-F]+|[0-9]+|.", re.S)
VALUE = re.compile(r"(?:[A-Za-z_]\w*|0[xX][0-9a-fA-F]+|[0-9]+)\Z")


class KeymapError(ValueError):
    pass


def parse_source(source):
    # Mask comments without changing offsets, so comments cannot impersonate a declaration.
    masked = re.sub(r"/\*.*?\*/|//[^\r\n]*", lambda m: " " * len(m[0]), source, flags=re.S)
    declarations = list(re.finditer(r"\bkey_table\s*\[[^\]]+\]\s*\[[^\]]+\]\s*=\s*\{", masked))
    if len(declarations) != 1:
        raise KeymapError("找不到唯一的二维 key_table 初始化定义。")
    tokens = [m for m in TOKEN.finditer(source, declarations[0].end() - 1)
              if not m[0].isspace() and not m[0].startswith(("/*", "//"))]
    position = 0

    def take(expected=None):
        nonlocal position
        if position >= len(tokens):
            raise KeymapError("key_table 不完整。")
        token = tokens[position]
        position += 1
        if expected is not None and token[0] != expected:
            raise KeymapError(f"key_table 格式不支持：预期 {expected}，实际 {token[0]}。")
        return token

    take("{")
    values, spans = [], []
    while tokens[position][0] != "}":
        take("{")
        row, offsets = [], []
        while tokens[position][0] != "}":
            token = take()
            if not VALUE.fullmatch(token[0]):
                raise KeymapError("仅支持常量名称或整数键值，不支持表达式及指定下标初始化。")
            row.append(token[0])
            offsets.append(token.span())
            if tokens[position][0] != "}":
                take(",")
        take("}")
        values.append(row)
        spans.append(offsets)
        if tokens[position][0] != "}":
            take(",")
    take("}")
    take(";")
    if len(values) != 2 or any(not 85 <= len(row) <= 88 for row in values):
        raise KeymapError("此布局需要两层，每层 85～88 个显式键值；文件未被修改。")
    return values, spans


def read_codes(header):
    text = Path(header).read_text(encoding="utf-8-sig")
    text = re.sub(r"/\*.*?\*/|//[^\r\n]*", "", text, flags=re.S)
    matches = re.findall(
        r"\b((?:KEYBOARD_|MODIFIER_|CONSUMER_)\w+)\s*=|"
        r"#define\s+(KBD_KEY_(?:NONE|FN|PAIRING|DEVICE_SWITCH|MACRO_[0-3]|MACRO_CANCEL))\b", text)
    return list(dict.fromkeys(first or second for first, second in matches))


def is_knob_code(code):
    return code == "KBD_KEY_NONE" or bool(re.fullmatch(r"CONSUMER_\w+", code))


def parse_knob_source(source):
    """Parse the explicit layer/direction designators, preserving token offsets."""
    masked = re.sub(r"/\*.*?\*/|//[^\r\n]*", lambda m: " " * len(m[0]), source, flags=re.S)
    declarations = list(re.finditer(r"\bknob_map\s*\[[^\]]+\]\s*\[[^\]]+\]\s*=\s*\{", masked))
    if not declarations and not re.search(r"\bknob_map\b", masked):
        return [], []  # Older keymaps can still edit physical keys.
    if len(declarations) != 1:
        raise KeymapError("找不到唯一的二维 knob_map 初始化定义。")
    tokens = iter(m for m in TOKEN.finditer(source, declarations[0].end() - 1)
                  if not m[0].isspace() and not m[0].startswith(("/*", "//")))
    current = next(tokens, None)

    def peek():
        return current[0] if current is not None else None

    def take(expected=None):
        nonlocal current
        token = current
        if token is None or (expected is not None and token[0] != expected):
            raise KeymapError("knob_map 格式不支持或不完整；需要 [0]/[1] 层和 [KBD_KNOB_CW]/[KBD_KNOB_CCW] 方向。")
        current = next(tokens, None)
        return token

    values, spans = [{}, {}], [{}, {}]
    seen_layers = set()
    take("{")
    while peek() != "}":
        take("[")
        layer_token = take()[0]
        if layer_token not in ("0", "1") or layer_token in seen_layers:
            raise KeymapError("knob_map 包含重复或不支持的层下标。")
        seen_layers.add(layer_token)
        layer = int(layer_token)
        take("]")
        take("=")
        take("{")
        while peek() != "}":
            take("[")
            direction = take()[0]
            if direction not in KNOB_DIRECTIONS or direction in values[layer]:
                raise KeymapError("knob_map 包含重复或不支持的旋转方向。")
            take("]")
            take("=")
            token = take()
            if not VALUE.fullmatch(token[0]):
                raise KeymapError("旋钮键值必须是常量名称或整数。")
            values[layer][direction] = token[0]
            spans[layer][direction] = token.span()
            if peek() != "}":
                take(",")
        take("}")
        if peek() != "}":
            take(",")
    take("}")
    take(";")
    if any(set(row) != set(KNOB_DIRECTIONS) for row in values):
        raise KeymapError("knob_map 必须显式配置两层的顺时针和逆时针功能。")
    return values, spans


class KeymapDocument:
    def __init__(self, path):
        self.path = Path(path).resolve()
        self.original_bytes = self.path.read_bytes()
        self.source = self.original_bytes.decode("utf-8")
        try:
            self.original, self.spans = parse_source(self.source)
        except IndexError as exc:
            raise KeymapError("key_table 不完整。") from exc
        self.values = [row.copy() for row in self.original]
        self.knob_original, self.knob_spans = parse_knob_source(self.source)
        self.knob_values = [row.copy() for row in self.knob_original]
        self.undo_stack = []

    @property
    def key_changes(self):
        return [(layer, index) for layer, row in enumerate(self.values)
                for index, code in enumerate(row) if code != self.original[layer][index]]

    @property
    def knob_changes(self):
        return [(layer, direction) for layer, row in enumerate(self.knob_values)
                for direction, code in row.items() if code != self.knob_original[layer][direction]]

    @property
    def changes(self):
        return self.key_changes + self.knob_changes

    def snapshot(self):
        return ([row.copy() for row in self.values], [row.copy() for row in self.knob_values])

    def assign_knob(self, layer, direction, code):
        if not self.knob_values:
            raise KeymapError("此文件没有 knob_map，无法编辑旋转功能。")
        if layer not in (0, 1) or direction not in KNOB_DIRECTIONS:
            raise KeymapError("无效的旋钮层或方向。")
        if not is_knob_code(code):
            raise KeymapError("旋钮旋转仅支持 CONSUMER_* 媒体功能或 KBD_KEY_NONE（禁用）。")
        if self.knob_values[layer][direction] != code:
            self.undo_stack.append(self.snapshot())
            self.knob_values[layer][direction] = code

    def assign(self, layer, index, code):
        if not VALUE.fullmatch(code):
            raise KeymapError("键值必须是 C 常量名称或整数。")
        before = self.snapshot()
        fn_positions = {i for row in self.values for i, value in enumerate(row) if value == FN}
        if code == FN:
            others = fn_positions - {index}
            if len(others) > 1:
                raise KeymapError("文件包含多个 Fn 位置，请先将多余 Fn 改为普通键，再移动 Fn。")
            for row in self.values:
                if others:
                    old = next(iter(others))
                    if old >= len(row):
                        raise KeymapError("Fn 所在位置在两层中的长度不一致。")
                    row[old] = row[index] if row[index] != FN else "KBD_KEY_NONE"
                row[index] = FN
        elif index in fn_positions:
            for row in self.values:
                row[index] = code
        else:
            self.values[layer][index] = code
        if before != self.snapshot():
            self.undo_stack.append(before)

    def undo(self):
        if self.undo_stack:
            self.values, self.knob_values = self.undo_stack.pop()

    def render(self):
        result = self.source
        replacements = [(self.spans[layer][index], self.values[layer][index])
                        for layer, index in self.key_changes]
        replacements.extend((self.knob_spans[layer][direction], self.knob_values[layer][direction])
                            for layer, direction in self.knob_changes)
        for (start, end), value in sorted(replacements, reverse=True):
            result = result[:start] + value + result[end:]
        return result.encode("utf-8")

    def save(self):
        if self.path.read_bytes() != self.original_bytes:
            raise KeymapError("文件已被其他程序修改。请先重新加载，避免覆盖外部修改。")
        if not self.changes:
            return None
        output = self.render()
        backup = self.path.with_name(self.path.name + "." + datetime.now().strftime("%Y%m%d-%H%M%S-%f") + ".bak")
        with backup.open("xb") as stream:
            stream.write(self.original_bytes)
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=self.path.parent, prefix=".keymap-", suffix=".tmp", delete=False) as stream:
                temporary = Path(stream.name)
                stream.write(output)
                stream.flush()
                os.fsync(stream.fileno())
            if self.path.read_bytes() != self.original_bytes:
                raise KeymapError("保存期间文件被外部修改，请重新加载。")
            os.replace(temporary, self.path)
        finally:
            if temporary is not None and temporary.exists():
                temporary.unlink()
        self.__init__(self.path)
        return backup
