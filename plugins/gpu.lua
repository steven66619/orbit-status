interval = 5

function tick()
    local f = io.popen(
        "nvidia-smi --query-gpu=utilization.gpu,memory.used,memory.total --format=csv,noheader,nounits 2>/dev/null || "
        .. "cat /sys/class/drm/card0/device/gpu_busy_percent 2>/dev/null || "
        .. "echo ?", "r")
    if not f then return "?" end
    local line = f:read()
    f:close()
    if not line then return "?" end
    if line:match("^%?") then return "?" end
    local gpu, mem_used, mem_total = line:match("(%d+).-,.(%d+),.(%d+)")
    if gpu then
        local mpct = math.floor(tonumber(mem_used) / tonumber(mem_total) * 100 + 0.5)
        return gpu .. "% " .. mpct .. "%"
    end
    local pct = line:match("(%d+)")
    if pct then return pct .. "%" end
    return "?"
end

function on_tooltip()
    local f = io.popen(
        "nvidia-smi 2>/dev/null | head -10 || cat /sys/class/drm/card0/device/gpu_busy_percent 2>/dev/null && echo GPU busy || echo no GPU info",
        "r")
    if not f then return "" end
    local r = f:read("*a")
    f:close()
    return r
end
