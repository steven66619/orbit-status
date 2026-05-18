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
    local out = ""
    if #parts >= 6 then
        out = "Filesystem: " .. parts[1] .. "\nSize: " .. parts[2] ..
               "\nUsed: " .. parts[3] .. " (" .. parts[5] .. ")" ..
               "\nFree: " .. parts[4] ..
               "\nMounted: " .. parts[6]
    end
    local tf = io.popen("hddtemp " .. path .. " 2>/dev/null || smartctl -A " .. path .. " 2>/dev/null | grep -i temp | head -1", "r")
    if tf then
        local t = tf:read()
        tf:close()
        if t then out = out .. "\nTemp: " .. t:gsub("^%s+", ""):gsub("%s+$", "") end
    end
    return out
end
