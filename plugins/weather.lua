interval = 300

function tick()
    local f = io.popen("curl -s 'wttr.in?format=%t+%C' 2>/dev/null", "r")
    if not f then return "?" end
    local line = f:read()
    f:close()
    if not line then return "?" end
    return (line:gsub("%s+", " "))
end
