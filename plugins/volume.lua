interval = 3

function tick()
    local f = io.popen("pamixer --get-volume 2>/dev/null", "r")
    if not f then return "?" end
    local vol = f:read()
    f:close()
    if not vol then return "?" end
    vol = (vol:gsub("%s+", ""))
    local mf = io.popen("pamixer --get-mute 2>/dev/null", "r")
    if mf then
        local m = mf:read()
        mf:close()
        if m and m:match("true") then
            return vol .. "% MUTED"
        end
    end
    return vol .. "%"
end

function on_tooltip()
    local f = io.popen("pamixer --get-volume 2>/dev/null && pamixer --get-mute 2>/dev/null", "r")
    if not f then return "" end
    local r = f:read("*a")
    f:close()
    return r
end
