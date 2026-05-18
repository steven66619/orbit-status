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
    local f = io.popen("df -h " .. path .. " 2>/dev/null | tail -1", "r")
    if not f then return "" end
    local line = f:read()
    f:close()
    if not line then return "" end
    local parts = {}
    for v in line:gmatch("%S+") do table.insert(parts, v) end
    if #parts >= 6 then
        return "Filesystem: " .. parts[1] .. "\nSize: " .. parts[2] ..
               "\nUsed: " .. parts[3] .. " (" .. parts[5] .. ")" ..
               "\nFree: " .. parts[4] ..
               "\nMounted: " .. parts[6]
    end
    return line
end
