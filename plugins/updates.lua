interval = 30

function tick()
    local n = 0
    local f = io.popen("pacman -Qu 2>/dev/null | wc -l", "r")
    if f then
        local c = f:read()
        f:close()
        if c then n = tonumber((c:gsub("%s+", ""))) or 0 end
    end
    local ff = io.popen("flatpak remote-ls --updates 2>/dev/null | wc -l", "r")
    if ff then
        local c = ff:read()
        ff:close()
        if c then n = n + (tonumber((c:gsub("%s+", ""))) or 0) end
    end
    return tostring(n)
end

function on_tooltip()
    local r = ""
    local f = io.popen("pacman -Qu 2>/dev/null", "r")
    if f then
        local pac = f:read("*a")
        f:close()
        if pac ~= "" then r = "pacman:\n" .. pac end
    end
    local ff = io.popen("flatpak remote-ls --updates 2>/dev/null", "r")
    if ff then
        local flat = ff:read("*a")
        ff:close()
        if flat ~= "" then
            if r ~= "" then r = r .. "\n" end
            r = r .. "flatpak:\n" .. flat
        end
    end
    if r == "" then r = "none" end
    return r
end
