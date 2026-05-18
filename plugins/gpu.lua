interval = 3

function tick()
    local f = io.popen("nvidia-smi --query-gpu=utilization.gpu,memory.used,memory.total --format=csv,noheader,nounits 2>/dev/null", "r")
    if f then
        local line = f:read()
        f:close()
        if line then
            local gpu, mem_used, mem_total = line:match("(%d+).-,.(%d+),.(%d+)")
            if gpu then
                local mpct = math.floor(tonumber(mem_used) / tonumber(mem_total) * 100 + 0.5)
                return gpu .. "% " .. mpct .. "%"
            end
        end
    end
    for _, card in ipairs({"card0", "card1", "card2"}) do
        local p = "/sys/class/drm/" .. card .. "/device/gpu_busy_percent"
        local pf = io.open(p, "r")
        if pf then
            local pct = pf:read()
            pf:close()
            if pct then
                pct = (pct:gsub("%s+", ""))
                local n = tonumber(pct)
                if n then return n .. "%" end
            end
        end
    end
    local rf = io.popen("timeout 1 radeontop -d - 2>/dev/null | tail -1", "r")
    if rf then
        local line = rf:read()
        rf:close()
        if line then
            local pct = line:match("gpu (%d+%.?%d*)%%")
            if pct then return math.floor(tonumber(pct) + 0.5) .. "%" end
        end
    end
    local hf = io.open("/sys/class/hwmon/hwmon4/freq1_input", "r")
    if hf then
        local khz = hf:read()
        hf:close()
        if khz then
            local mhz = math.floor(tonumber(khz) / 1000000 + 0.5)
            return tostring(mhz) .. "MHz"
        end
    end
    return "?"
end

function on_tooltip()
    local r = ""
    local nf = io.popen("nvidia-smi 2>/dev/null | head -10", "r")
    if nf then r = nf:read("*a"); nf:close() end
    if r ~= "" then return r end
    local rf = io.popen("timeout 1 radeontop -d - 2>/dev/null | tail -1", "r")
    if rf then r = rf:read("*a"); rf:close() end
    if r ~= "" then return r end
    return "no GPU info"
end
