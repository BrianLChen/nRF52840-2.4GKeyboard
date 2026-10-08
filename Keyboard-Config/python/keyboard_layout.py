"""Physical geometry in key units, bound to immutable PCB scan indices."""
from dataclasses import dataclass


@dataclass(frozen=True)
class Key:
    index: int
    legend: str
    x: float
    y: float
    width: float = 1
    knob: bool = False


KEYS = []


def row(y, entries, x=0):
    for index, legend, width in entries:
        KEYS.append(Key(index, legend, x, y, width))
        x += width


KEYS.append(Key(6, "Esc", 0, 0))
row(0, [(16,"F1",1),(17,"F2",1),(26,"F3",1),(27,"F4",1)], 2)
row(0, [(37,"F5",1),(47,"F6",1),(48,"F7",1),(59,"F8",1)], 6.5)
row(0, [(60,"F9",1),(66,"F10",1),(67,"F11",1),(68,"F12",1)], 11)
row(0, [(79,"Delete",1),(80,"PrtSc",1)], 15.25)
row(1.5, [(5,"` ~",1),(7,"1 !",1),(15,"2 @",1),(18,"3 #",1),
          (25,"4 $",1),(28,"5 %",1),(36,"6 ^",1),(38,"7 &",1),
          (46,"8 *",1),(49,"9 (",1),(58,"0 )",1),(61,"- _",1),
          (65,"= +",1),(69,"Backspace",2)])
row(1.5, [(78,"Home",1),(81,"PgUp",1)], 15.25)
row(2.5, [(4,"Tab",1.5),(8,"Q",1),(14,"W",1),(19,"E",1),(24,"R",1),
          (29,"T",1),(35,"Y",1),(39,"U",1),(45,"I",1),(50,"O",1),(57,"P",1),
          (62,"[ {",1),(64,"] }",1),(70,"\\ |",1.5)])
row(2.5, [(77,"End",1),(82,"PgDn",1)], 15.25)
row(3.5, [(3,"Caps Lock",1.75),(9,"A",1),(13,"S",1),(20,"D",1),(23,"F",1),
          (30,"G",1),(34,"H",1),(40,"J",1),(44,"K",1),(51,"L",1),
          (56,"; :",1),(63,"' \"",1),(71,"Enter",2.25)])
row(4.5, [(2,"Shift",2.25),(10,"Z",1),(12,"X",1),(21,"C",1),(22,"V",1),
          (31,"B",1),(33,"N",1),(41,"M",1),(43,", <",1),(52,". >",1),
          (55,"/ ?",1),(72,"Shift",2.75)])
KEYS.append(Key(76, "↑", 15.25, 4.5))
KEYS.append(Key(84, "旋钮按压", 16.25, 4.05, knob=True))
row(5.5, [(0,"Ctrl",1.25),(1,"Win",1.25),(11,"Alt",1.25),(32,"Space",6.25),
          (42,"Alt",1),(53,"Win",1),(54,"Fn",1),(73,"Ctrl",1)])
row(5.5, [(74,"←",1),(75,"↓",1),(83,"→",1)], 14.25)
KEY_BY_INDEX = {key.index: key for key in KEYS}
assert sorted(KEY_BY_INDEX) == list(range(85)) and len(KEYS) == 85


LABELS = {
    "KBD_KEY_NONE": "禁用", "KBD_KEY_FN": "Fn", "KBD_KEY_PAIRING": "配对",
    "KBD_KEY_DEVICE_SWITCH": "切换设备", "KEYBOARD_SPACEBAR": "Space",
    "KBD_KEY_MACRO_0": "宏 0", "KBD_KEY_MACRO_1": "宏 1",
    "KBD_KEY_MACRO_2": "宏 2", "KBD_KEY_MACRO_3": "宏 3",
    "KBD_KEY_MACRO_CANCEL": "取消宏",
    "KEYBOARD_ESCAPE": "Esc", "KEYBOARD_BACKSPACE": "Backspace",
    "KEYBOARD_CAPS_LOCK": "Caps Lock", "KEYBOARD_PRINTSCREEN": "PrtSc",
    "KEYBOARD_PAGEUP": "PgUp", "KEYBOARD_PAGEDOWN": "PgDn",
    "KEYBOARD_SCROLL_LOCK": "Scroll Lock", "KEYBOARD_SEMI_COLON": "; :",
    "KEYBOARD_QUOTE": "' \"", "KEYBOARD_TILDE": "` ~", "KEYBOARD_MINUS": "- _",
    "KEYBOARD_PLUS": "= +", "KEYBOARD_OPEN_BRACKET": "[ {", "KEYBOARD_CLOSE_BRACKET": "] }",
    "KEYBOARD_BACKSLASH": "\\ |", "KEYBOARD_SLASH": "/ ?", "KEYBOARD_COMMA": ", <",
    "KEYBOARD_PERIOD": ". >", "KEYBOARD_LEFT": "←", "KEYBOARD_RIGHT": "→",
    "KEYBOARD_UP": "↑", "KEYBOARD_DOWN": "↓", "KEYBOARD_NONE_US": "Non-US #",
    "CONSUMER_MUTE": "静音", "CONSUMER_PLAY_PAUSE": "播放/暂停",
    "CONSUMER_VOLUME_INCREASE": "音量 +", "CONSUMER_VOLUME_DECREASE": "音量 −",
    "CONSUMER_NEXT_TRACK": "下一曲", "CONSUMER_PREVIOUS_TRACK": "上一曲",
    "CONSUMER_STOP": "停止", "CONSUMER_AL_CALCULATOR": "计算器",
}
for side, chinese in [("LEFT", "左"), ("RIGHT", "右")]:
    for suffix, label in [("CTRL", "Ctrl"), ("SHIFT", "Shift"), ("ALT", "Alt"), ("UI", "Win")]:
        LABELS[f"MODIFIER_{side}_{suffix}"] = chinese + " " + label


def label(code):
    if code in LABELS:
        return LABELS[code]
    if code.startswith("KEYBOARD_"):
        value = code.removeprefix("KEYBOARD_")
        return value if len(value) == 1 or (value.startswith("F") and value[1:].isdigit()) else value.replace("_", " ").title()
    return code


def category(code):
    if code.startswith("KBD_KEY_MACRO_"):
        return "宏"
    if code.startswith("CONSUMER_"):
        return "媒体"
    if code.startswith("KBD_KEY_"):
        return "内部功能"
    if code.startswith("MODIFIER_"):
        return "修饰键"
    return "普通按键"
