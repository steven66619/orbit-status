interval = 30

function tick()
    local f = io.popen("pacman -Qu 2>/dev/null | wc -l", "r")
    if not f then return "0" end
    local c = f:read()
    f:close()
    if not c then return "0" end
    c = (c:gsub("%s+", ""))
    local n = tonumber(c)
    return tostring(n or 0)
end

function on_tooltip()
    local f = io.popen("pacman -Qu 2>/dev/null | head -8 || echo none", "r")
    if not f then return "" end
    local r = f:read("*a")
    f:close()
    return r
end
