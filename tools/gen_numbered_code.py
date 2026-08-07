#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成规范化带行号的代码文本（codeNumbered.txt）

覆盖范围：main/ 下全部代码文件（.cpp / .hpp / .h / .c / .inl / .inc）
排序依据：模块 & 重要程度（入口 main.cpp 最先 → 基础设施 → 显示/输入/媒体 → 网络/联机 → 应用层）
输出格式：
    文件路径：<path>    模块说明：<desc>
        <行号>.  <代码行>
规则：
    - 行号跨所有文件全局连续（路径/说明行不计号）
    - 删除"仅有注释"的行；行尾注释保留
    - 压缩连续空行为单个空行；去掉文件首尾空行
    - 模块说明取自 codeIntro.md 的功能描述（取首个句号前内容）
"""
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAIN_DIR = os.path.join(ROOT, 'main')
INTRO = os.path.join(ROOT, 'codeIntro.md')
OUT = os.path.join(ROOT, 'codeNumbered.txt')
EXTS = ('.cpp', '.hpp', '.h', '.c', '.inl', '.inc')

# ---------------------------------------------------------------------------
# 1. 模块 & 重要程度权重（数值越小越靠前；文件取"最长匹配"前缀）
# ---------------------------------------------------------------------------
MODULES = [
    ('main/main.cpp', 0),            # 程序入口
    ('main/task', 10),               # 协程调度器（核心基础设施）
    ('main/mutex', 20),
    ('main/gpio', 30),
    ('main/iic', 40),
    ('main/gui', 50),
    ('main/utility', 60),
    ('main/storage', 70),            # FAT/memFS/SD/VFS
    ('main/monitor', 80),
    ('main/power', 90),
    ('main/battery', 100),
    ('main/display', 110),           # 显示/触摸/串流/音频
    ('main/touch', 120),
    ('main/virtualIndev', 130),
    ('main/screenStream', 140),
    ('main/audio', 150),
    ('main/gamepad', 160),           # BLE 手柄 + 统一输入
    ('main/aiAgent', 170),           # AI Agent
    ('main/lua', 180),               # Lua 沙箱引擎
    ('main/market', 190),            # 游戏市场
    ('main/wifi', 200),              # 网络层
    ('main/server', 210),
    ('main/websocket', 220),
    ('main/host', 230),
    ('main/discovery', 240),
    ('main/app/app', 250),           # App 核心
    ('main/app/appStack', 251),
    ('main/app/appStackManager', 252),
    ('main/app/appFactory', 253),
    ('main/app/desktopApp', 254),    # 桌面（主界面）
    ('main/app/aiAgentApp', 255),    # AI 助手浮窗
    ('main/app/wifiSettingsApp', 256),
    ('main/app/bleSettingsApp', 257),
    ('main/app/timeSettingsApp', 258),
    ('main/app/powerManagementApp', 259),
    ('main/app/consoleManager', 260),
    ('main/app/luaGameApp', 261),
    ('main/app/luaManagerApp', 262),
    ('main/app/marketApp', 263),
    ('main/app/chineseChess', 264),
    ('main/app/fruitNinja', 265),
    ('main/app/snake', 266),
    ('main/app/tetris', 267),
    ('main/app/testApp', 268),
    ('reserces/server/config', 269),
    ('reserces/server/gamepad', 270),
    ('reserces/server/screen', 271),
    ('reserces/server/server', 272),
    ('reserces/server/tetris', 273),
    ('reserces/server/index', 274),
    
]

HEADER_EXTS = ('hpp', 'h', 'inc', 'inl', "html", "js")   # 头文件/内联优先于实现文件


def file_rank(rel):
    """返回排序 key：(模块权重, 头文件优先, 路径)"""
    best_w, best_len = 10 ** 9, -1
    for prefix, w in MODULES:
        # 前缀匹配：精确 / 子目录(main/task/...) / 同名前缀文件(main/app/app.cpp)
        if rel == prefix or rel.startswith(prefix + '/') or rel.startswith(prefix + '.'):
            if len(prefix) > best_len:
                best_len, best_w = len(prefix), w
    is_hdr = 0 if rel.rsplit('.', 1)[-1] in HEADER_EXTS else 1
    return (best_w, is_hdr, rel)


# ---------------------------------------------------------------------------
# 2. 逐行扫描：判断一行是否为"纯注释行"（不含任何代码）
# ---------------------------------------------------------------------------
def scan_non_comment(line, in_block):
    """返回 (非注释代码部分, 处理后块注释是否仍打开)。"""
    code, i, n = [], 0, len(line)
    while i < n:
        if in_block:
            idx = line.find('*/', i)
            if idx == -1:
                return ''.join(code), True     # 整行仍在块注释内
            i = idx + 2
            in_block = False
            continue
        c = line[i]
        if c == '/' and i + 1 < n:
            nxt = line[i + 1]
            if nxt == '/':                      # 行注释：之后全部忽略
                break
            if nxt == '*':                      # 进入块注释
                in_block = True
                i += 2
                continue
        if c in ('"', "'"):                     # 跳过字符串/字符字面量
            q = c
            code.append(c)
            i += 1
            while i < n:
                if line[i] == '\\':
                    code.append(line[i])
                    if i + 1 < n:
                        code.append(line[i + 1])
                    i += 2
                    continue
                code.append(line[i])
                if line[i] == q:
                    i += 1
                    break
                i += 1
            continue
        code.append(c)
        i += 1
    return ''.join(code), in_block


def process_file(path):
    """读取文件 → 删纯注释行 → 压缩连续空行 → 去首尾空行。返回保留行列表。"""
    with open(path, encoding='utf-8-sig') as f:   # utf-8-sig 自动去 BOM
        raw_lines = f.read().splitlines()

    kept, in_block = [], False
    for line in raw_lines:
        if line.strip() == '':
            kept.append('')
            continue
        code, in_block = scan_non_comment(line, in_block)
        if code.strip() == '':
            continue                              # 纯注释行：删除
        kept.append(line)

    # 压缩连续空行为单个空行
    out, prev_blank = [], False
    for line in kept:
        if line == '':
            if prev_blank:
                continue
            prev_blank = True
            out.append('')
        else:
            prev_blank = False
            out.append(line)
    # 去掉首尾空行
    while out and out[0] == '':
        out.pop(0)
    while out and out[-1] == '':
        out.pop()
    return out


# ---------------------------------------------------------------------------
# 3. 从 codeIntro.md 读取 路径 → 功能描述
# ---------------------------------------------------------------------------
def load_desc():
    desc, cur = {}, None
    with open(INTRO, encoding='utf-8') as f:
        for line in f:
            m = re.match(r'^## FILE: (.+)$', line)
            if m:
                cur = m.group(1).strip()
                continue
            m2 = re.match(r'^功能[:：]\s*(.*)$', line)
            if m2 and cur:
                desc[cur] = m2.group(1).strip()
    return desc


def short_desc(text):
    if not text:
        return '（未提供）'
    text = re.sub(r'[`*]', '', text)               # 去 markdown 标记
    idx = text.find('。')
    return text[:idx] if idx > 0 else text


def main():
    # 收集全部代码文件
    files = []
    for dp, _dn, fns in os.walk(MAIN_DIR):
        for fn in fns:
            if fn.endswith(EXTS):
                rel = os.path.relpath(os.path.join(dp, fn), ROOT)
                files.append(rel)
    files.sort(key=file_rank)

    desc = load_desc()
    out_lines, lineno, missing = [], 0, []
    for rel in files:
        if rel not in desc:
            missing.append(rel)
        body = process_file(os.path.join(ROOT, rel))
        out_lines.append('文件路径：{}    模块说明：{}'.format(rel, short_desc(desc.get(rel))))
        for l in body:
            lineno += 1
            out_lines.append('\t{}.  {}'.format(lineno, l))
        # 文件之间不插入空行（行号保持连续）

    with open(OUT, 'w', encoding='utf-8') as f:
        f.write('\n'.join(out_lines) + '\n')

    print('输出文件: {}'.format(OUT))
    print('文件数: {}'.format(len(files)))
    print('编号代码行数(全局连续): {}'.format(lineno))
    print('文件头+代码总行数: {}'.format(len(out_lines)))
    print('缺少模块说明: {}'.format(len(missing)))
    for x in missing:
        print('  - ' + x)
    # 校验无重复文件
    assert len(files) == len(set(files)), '存在重复文件！'


if __name__ == '__main__':
    main()
