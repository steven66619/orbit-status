interval = 120

local path = "/"

function tick()
    local f = io.popen("df " .. path .. " 2>/dev/null | tail -1", "r")
    if not f then return "?" end
    local line = f:read()
    f:close()
    if not line then return "?" end
    local pct = line:match("(%d+)%%")
    if pct then return pct .. "%" end
    return "?"
end

function on_tooltip()
    local f = io.popen("df -h 2>/dev/null", "r")
    if not f then return "" end
    local r = f:read("*a")
    f:close()
    return r
end
