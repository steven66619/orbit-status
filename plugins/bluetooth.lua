interval = 10

function tick()
    local f = io.popen("bluetoothctl devices Connected 2>/dev/null | head -1", "r")
    if not f then return "?" end
    local line = f:read()
    f:close()
    if line and line ~= "" then
        local name = line:match("Device%x+%s+(.*)")
        if name then return " " .. name end
        return ""
    end
    local ff = io.popen("bluetoothctl show 2>/dev/null | grep 'Powered:' | awk '{print $2}'", "r")
    if ff then
        local pw = ff:read()
        ff:close()
        if pw and pw:match("yes") then return "" end
    end
    return ""
end

function on_tooltip()
    local f = io.popen("bluetoothctl devices Connected 2>/dev/null || echo none", "r")
    if not f then return "" end
    local r = f:read("*a")
    f:close()
    return r
end
