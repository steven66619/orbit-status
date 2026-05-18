interval = 5

function tick()
    local f = io.popen("brightnessctl -m 2>/dev/null | head -1", "r")
    if not f then return "?" end
    local line = f:read()
    f:close()
    if not line then return "?" end
    local pct = line:match(",(%d+)%%")
    if pct then return pct .. "%" end
    return "?"
end

function on_scroll(direction)
    if direction > 0 then
        os.execute("brightnessctl set +5% 2>/dev/null")
    else
        os.execute("brightnessctl set 5%- 2>/dev/null")
    end
end

function on_tooltip()
    local f = io.popen("brightnessctl -m 2>/dev/null", "r")
    if not f then return "" end
    local r = f:read("*a")
    f:close()
    return r
end
