interval = 10

function tick()
    local f = io.popen("nmcli -t -f active,ssid dev wifi 2>/dev/null | grep '^yes:' | cut -d: -f2", "r")
    if not f then return "NO NET" end
    local ssid = f:read()
    f:close()
    if ssid then
        ssid = (ssid:gsub("%s+", ""))
        if ssid ~= "" then return ssid end
    end
    local iface_f = io.popen("iw dev 2>/dev/null | awk '/Interface/{print $2; exit}'", "r")
    if iface_f then
        local iface = iface_f:read()
        iface_f:close()
        if iface then
            iface = (iface:gsub("%s+", ""))
            local lf = io.popen("iw dev " .. iface .. " link 2>/dev/null | awk '/SSID/{print $2}'", "r")
            if lf then
                local ssid2 = lf:read()
                lf:close()
                if ssid2 then
                    ssid2 = (ssid2:gsub("%s+", ""))
                    if ssid2 ~= "" then return ssid2 end
                end
            end
        end
    end
    return "NO NET"
end

function on_tooltip()
    local f = io.popen("iw dev 2>/dev/null | awk '/Interface/{print $2}' | head -1 | xargs -r iw dev link 2>/dev/null || nmcli -t dev status 2>/dev/null | head -3", "r")
    if not f then return "" end
    local r = f:read("*a")
    f:close()
    return r
end
