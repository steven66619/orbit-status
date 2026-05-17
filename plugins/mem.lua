interval = 5

function tick()
    local f = io.open("/proc/meminfo", "r")
    if not f then return "?" end
    local total, available = 0, 0
    for line in f:lines() do
        local v = line:match("MemTotal:%s+(%d+)")
        if v then total = tonumber(v) end
        v = line:match("MemAvailable:%s+(%d+)")
        if v then available = tonumber(v); break end
    end
    f:close()
    if total > 0 then
        return math.floor(100 * (total - available) / total) .. "%"
    end
    return "?"
end

function on_tooltip()
    local f = io.popen("ps -eo pid,%mem,comm --sort=-%mem 2>/dev/null | head -6", "r")
    if not f then return "" end
    local r = f:read("*a")
    f:close()
    return r
end
