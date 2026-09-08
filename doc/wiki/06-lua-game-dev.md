# Lua 游戏开发指南

## 概述

The-Defect 内置 Lua 5.5 沙箱引擎，可以运行 Lua 编写的游戏。Lua 游戏通过 AI 助手生成或从游戏市场下载，运行在安全的沙箱环境中。

## 游戏契约

每个 Lua 游戏**必须**实现以下两个全局函数：

```lua
function setup()
    -- 初始化游戏状态（可选）
    -- 在游戏启动时调用一次
end

function loop(dt)
    -- 每帧调用（~60fps）
    -- dt = 距上帧的时间（秒）
    -- 返回 true 继续运行，返回 false 退出游戏
    return true
end
```

## 可用模块

### display — 绘图

```lua
local display = require("display")

-- 屏幕尺寸
local W = display.width()   -- 1280（横屏）
local H = display.height()  -- 720

-- 颜色（三种格式）
local red = "#ff0000"
local red = 0xff0000
local red = {r=255, g=0, b=0}

-- 基本绘图
display.fill_screen("#1a1a2e")           -- 清屏
display.fill_rect(x, y, w, h, color)    -- 填充矩形
display.draw_rect(x, y, w, h, color)    -- 矩形边框
display.draw_line(x1, y1, x2, y2, color, width)  -- 线段
display.draw_circle(cx, cy, r, color)    -- 圆形（填充）
display.draw_text(x, y, text, color, font_size)  -- 文字
-- font_size: 24(小) / 32(中) / 56(大)
```

**性能提示**：display 模块使用 LVGL 对象池化，参数不变时自动跳过更新。保持每帧绘制顺序稳定可获得最佳性能。

### input — 手柄输入

```lua
local input = require("input")

-- 状态查询（帧快照）
if input.is_pressed(input.BTN_A) then end      -- A 键是否按下
if input.is_just_pressed(input.BTN_A) then end -- A 键刚按下（上升沿）

-- 摇杆
local lx, ly = input.joystick(0)  -- 左摇杆 (-128 ~ 127)
local rx, ry = input.joystick(1)  -- 右摇杆

-- 按钮常量
-- input.BTN_A, input.BTN_B, input.BTN_X, input.BTN_Y
-- input.BTN_START, input.BTN_SELECT
-- input.BTN_L3, input.BTN_R3

-- 设备信息
local count = input.player_count()     -- 连接手柄数
local connected = input.connected()    -- 是否有手柄
```

### touch — 触摸输入

```lua
local touch = require("touch")

-- 多点触控
local count = touch.count()          -- 当前触摸点数
if touch.is_down(0) then end        -- 槽位 0 是否按下
local x, y = touch.xy(0)            -- 槽位 0 坐标
if touch.is_just_pressed(0) then end   -- 刚按下
if touch.is_just_released(0) then end  -- 刚释放

-- 获取所有触摸点
local points = touch.all()
for i, p in ipairs(points) do
    print(p.id, p.x, p.y, p.down)
end
```

### audio — 音频

```lua
local audio = require("audio")

-- 播放音效
audio.play_sfx("/root/sfx/click.wav")

-- 播放背景音乐
audio.play_bgm("/root/music/bgm.mp3")

-- 停止背景音乐
audio.stop_bgm()

-- 设置音量 (0-100)
audio.set_volume(80)
```

### storage — 存储

```lua
local storage = require("storage")

-- 保存数据（限 /root/lua/data/ 目录，单文件 ≤256KB）
storage.save("score.json", '{"high_score":1000}')

-- 读取数据
local data = storage.load("score.json")

-- 删除文件
storage.remove("score.json")

-- 检查文件是否存在
if storage.exists("score.json") then end
```

### thread — 线程与延时

```lua
local thread = require("thread")

-- 延时（毫秒）
thread.sleep(100)

-- 获取当前时间（毫秒）
local now = thread.now()

-- 随机数
local r = thread.random(1, 100)  -- 1 到 100 的随机整数
```

## 完整示例：弹球游戏

```lua
local display = require("display")
local input = require("input")

local W, H = display.width(), display.height()

-- 球的状态
local ball = { x = W/2, y = H/2, vx = 300, vy = 200, r = 10 }
local score = 0

function setup()
    display.fill_screen("#1a1a2e")
end

function loop(dt)
    -- 更新位置
    ball.x = ball.x + ball.vx * dt
    ball.y = ball.y + ball.vy * dt

    -- 碰撞检测
    if ball.x < ball.r or ball.x > W - ball.r then
        ball.vx = -ball.vx
        score = score + 1
    end
    if ball.y < ball.r or ball.y > H - ball.r then
        ball.vy = -ball.vy
    end

    -- 手柄控制
    if input.is_pressed(input.BTN_B) then
        return false  -- 退出游戏
    end

    -- 渲染
    display.fill_screen("#1a1a2e")
    display.draw_circle(ball.x, ball.y, ball.r, "#ff3b30")
    display.draw_text(10, 10, "Score: " .. score, "#ffffff", 32)

    return true
end
```

## 文件系统

Lua 脚本存储在 FAT 分区：

| 路径 | 说明 |
|------|------|
| `/root/lua/game/` | 本地 Lua 游戏脚本 |
| `/root/lua/market/` | 市场下载的 Lua 游戏 |
| `/root/lua/data/` | 游戏存档数据（storage 模块专用） |

## 运行方式

1. **AI 生成**：对 AI 助手说"帮我做一个贪吃蛇游戏"，AI 会生成 Lua 代码并直接运行
2. **市场下载**：从游戏市场浏览 Lua 游戏，点击下载
3. **手动上传**：通过文件管理页面 `http://<主机名>.local/file` 上传 `.lua` 文件

## ⚠ 安全限制

Lua 运行在沙箱环境中，以下功能被**禁止**：

| 被禁模块 | 说明 |
|----------|------|
| `io` | 不能直接读写文件（用 `storage` 模块） |
| `os` | 不能执行系统命令 |
| `package` | 不能加载 C 模块 |
| `debug` | 不能调试/内省 |
| `dofile` / `loadfile` | 不能加载外部文件 |

文件读写只能通过 `storage` 模块，限定在 `/root/lua/data/` 目录。

## ⚠ 已知限制

- 每帧指令计数上限 1000 万条（防死循环）
- 单文件最大 256KB
- 不支持协程的跨帧状态保持（每帧独立调用 `loop(dt)`）
- LVGL 对象池上限 64 个（复杂游戏可能不够）
