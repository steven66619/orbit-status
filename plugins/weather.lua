interval = 300

local function get_location()
    local home = os.getenv("HOME")
    if not home then return "" end
    local f = io.open(home .. "/.config/orbit-status/config", "r")
    if not f then return "" end
    for line in f:lines() do
        line = line:gsub("#.*$", "")
        local key, val = line:match("^%s*([%w_]+)%s*=%s*(.-)%s*$")
        if key and val and key == "weather_location" then
            f:close()
            return val:gsub("^\"(.*)\"$", "%1")
        end
    end
    f:close()
    return ""
end

function tick()
    local loc = get_location()
    if loc ~= "" then loc = "/" .. loc:gsub(" ", "+") end
    local cmd = "curl -s -H 'Accept: text/plain' -H 'User-Agent: curl' 'wttr.in" .. loc .. "?format=%t+%C' 2>/dev/null"
    local f = io.popen(cmd, "r")
    if not f then return "?" end
    local line = f:read()
    f:close()
    if not line then return "?" end
    line = line:gsub("%s+", " ")
    line = line:gsub("^%+", "")
    return line
end