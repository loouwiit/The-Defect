-- /root/lua/test_display.lua
-- Lua 引擎集成手动验证脚本

function setup()
    -- 可选初始化
end

function loop(dt)
    -- 清屏
    display.fill_screen("#0c1220")

    -- 获取实际分辨率
    local w = display.width()
    local h = display.height()

    -- 显示分辨率信息
    display.draw_text(20, 20, "Resolution: " .. w .. "x" .. h, 24, "#888888")

    -- 基本图形绘制
    display.fill_rect(50, 60, 100, 80, "#ff0000")        -- 红色矩形
    display.draw_rect(50, 160, 100, 80, "#00ff00")       -- 绿色空心矩形
    display.draw_circle(350, 100, 50, "#ff8800")         -- 橙色圆形
    display.draw_text(50, 280, "Hello Lua!", 32, "#ffffff")  -- 白色文字

    -- 画线
    display.draw_line(50, 340, w - 50, 340, "#00aaff")

    -- 显示玩家信息
    local pCount = input.player_count()
    display.draw_text(50, 380, "Players: " .. pCount, 32, "#ffffff")

    -- 遍历各玩家
    for p = 0, 3 do
        if input.connected(p) then
            local y = 430 + p * 45
            display.draw_text(50, y, "P" .. (p+1) .. ": connected", 24, "#88ff88")

            -- 摇杆
            local j = input.joystick("left", p)
            if j.x ~= 128 or j.y ~= 128 then
                display.draw_text(250, y, "LX:" .. j.x .. " LY:" .. j.y, 24, "#00aaff")
            end

            -- 按钮
            if input.is_pressed("a", p) then
                display.draw_text(500, y, "A", 24, "#ffaa00")
            end
            if input.is_pressed("b", p) then
                display.draw_text(530, y, "B", 24, "#ff4444")
            end
        end
    end

    -- 一秒后显示退出提示
    if thread.now() > 1000 then
        display.draw_text(200, 1200, "Press START to exit", 24, "#666666")
    end

    -- P1 或 P2 按 Start 退出
    if input.is_just_pressed("start", 0) or input.is_just_pressed("start", 1) then
        return false
    end

    return true
end
