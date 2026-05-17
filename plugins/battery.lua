interval = 10

function tick()
    local f = io.open("/sys/class/power_supply/BAT0/uevent", "r")
    if not f then return "" end
    local cap, charging = nil, false
    for line in f:lines() do
        local v = line:match("POWER_SUPPLY_CAPACITY=(%d+)")
        if v then cap = tonumber(v) end
        if line:match("POWER_SUPPLY_STATUS=Charging") then charging = true end
        if line:match("POWER_SUPPLY_STATUS=Full") then cap = 100 end
    end
    f:close()
    if not cap then return "" end
    if charging then return cap .. "%+" end
    return cap .. "%"
end

function on_tooltip()
    local f = io.popen("cat /sys/class/power_supply/BAT0/uevent 2>/dev/null || echo no battery", "r")
    if not f then return "" end
    local r = f:read("*a")
    f:close()
    return r
end
