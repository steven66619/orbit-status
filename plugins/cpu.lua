interval = 2

local prev_total = 0
local prev_idle = 0

function tick()
    local f = io.open("/proc/stat", "r")
    if not f then return "?" end
    local line = f:read()
    f:close()
    local user, nice, sys, idle = line:match("cpu%s+(%d+)%s+(%d+)%s+(%d+)%s+(%d+)")
    if not user then return "?" end
    user = tonumber(user); nice = tonumber(nice); sys = tonumber(sys); idle = tonumber(idle)
    local total = user + nice + sys + idle
    local dtotal = total - prev_total
    local didle = idle - prev_idle
    prev_total = total
    prev_idle = idle
    if dtotal > 0 then
        return math.floor(100 * (dtotal - didle) / dtotal) .. "%"
    end
    return "0%"
end

function on_tooltip()
    local f = io.popen("ps -eo pid,%cpu,comm --sort=-%cpu 2>/dev/null | head -6", "r")
    if not f then return "" end
    local r = f:read("*a")
    f:close()
    return r
end
