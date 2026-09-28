#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
WatchBar 注入配置修改器（CnCNet 客户端 ClientDefinitions.ini）。

批处理里做字符串手术太容易出错：cmd 的 `!var:OLD=NEW!` 会把匹配串当分隔符
并在自己的输出里再次替换，`%` 也会被变量展开。所以这部分逻辑放在 Python 里，
只做一次字面替换，并且要求锚点唯一。

用法：
    python patch_config.py check  <config> <dll>
    python patch_config.py plan   <config> <dll>
    python patch_config.py apply  <config> <dll>

退出码：0 = 已包含 / 成功；1 = 未包含（check）；3 = 找不到唯一锚点，拒绝修改。
"""
import sys

# 旧名字：换名时自动替换，省得手工改配置行。
LEGACY = "-i=ECObserver.dll"


def read(path):
    # 游戏的 ini 是 latin-1，不能用 utf-8，否则中文注释会解码失败
    with open(path, "r", encoding="latin-1") as f:
        return f.read()


def anchor_line(src, marker):
    for line in src.splitlines():
        if marker in line:
            return line
    return None


def compute(src, dll):
    """返回 (新文本, 说明行列表)，或 (None, 失败原因列表)。"""
    want = "-i=" + dll

    if want in src:
        return src, ["  already patched"]

    # 1) 旧名换新名：原地替换，位置不动。
    if LEGACY in src:
        if src.count(LEGACY) != 1:
            return None, ["  REFUSED: found %d copies of %r" % (src.count(LEGACY), LEGACY)]
        before = anchor_line(src, "ExtraCommandLineParams")
        after = before.replace(LEGACY, want)
        return src.replace(LEGACY, want), ["  - %s" % before, "  + %s" % after]

    # 2) 追加：优先跟在 Phobos 后面，其次 Ares，最后 gamemd.exe。
    for anchor in ("-i=Phobos.dll", "-i=Ares.dll", "gamemd.exe"):
        if anchor in src:
            if src.count(anchor) != 1:
                return None, ["  REFUSED: found %d copies of %r" % (src.count(anchor), anchor)]
            before = anchor_line(src, "ExtraCommandLineParams")
            after = before.replace(anchor, anchor + " " + want)
            return src.replace(anchor, anchor + " " + want), \
                   ["  - %s" % before, "  + %s" % after]

    return None, ["  REFUSED: no usable anchor (-i=Phobos.dll / -i=Ares.dll / gamemd.exe) "
                  "on the ExtraCommandLineParams line. Add %r by hand." % want]


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 2

    mode, cfg, dll = sys.argv[1], sys.argv[2], sys.argv[3]
    src = read(cfg)

    if mode == "check":
        return 0 if ("-i=" + dll) in src else 1

    if mode in ("plan", "apply"):
        new, notes = compute(src, dll)
        for n in notes:
            print(n)
        if new is None:
            return 3
        if mode == "apply" and new != src:
            with open(cfg, "w", encoding="latin-1") as f:
                f.write(new)
            print("  written.")
        return 0

    print("unknown mode: %s" % mode)
    return 2


if __name__ == "__main__":
    sys.exit(main())
