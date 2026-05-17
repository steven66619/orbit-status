interval = 3

function tick()
    local f = io.popen("pamixer --get-volume 2>/dev/null", "r")
    if f then
        local vol = f:read()
        f:close()
        if vol then
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
    end

    local f2 = io.popen("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", "r")
    if f2 then
        local line = f2:read()
        f2:close()
        if line then
            local pct = line:match("([%d.]+)")
            if pct then
                local vol = math.floor(tonumber(pct) * 100 + 0.5)
                if line:find("MUTED") then
                    return vol .. "% MUTED"
                end
                return vol .. "%"
            end
        end
    end
    return "--"
end

function on_tooltip()
    local f = io.popen("pamixer --get-volume 2>/dev/null && pamixer --get-mute 2>/dev/null || wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null || echo no audio backend", "r")
    if not f then return "" end
    local r = f:read("*a")
    f:close()
    return r
end
