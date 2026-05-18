interval = 2

local prev_rx = 0
local prev_tx = 0
local prev_time = 0

local function get_bytes()
    local f = io.open("/proc/net/dev", "r")
    if not f then return nil end
    local rx, tx = 0, 0
    for line in f:lines() do
        local name, r, t = line:match("^%s*(%w+):%s+(%d+)%s+%d+%s+%d+%s+%d+%s+%d+%s+%d+%s+%d+%s+%d+%s+(%d+)")
        if name and name ~= "lo" then
            rx = rx + tonumber(r)
            tx = tx + tonumber(t)
        end
    end
    f:close()
    return rx, tx
end

local function fmt(bps)
    if not bps then return "0b" end
    if bps < 1000 then return math.floor(bps) .. "b"
    elseif bps < 1000000 then return string.format("%.1fK", bps / 1000)
    else return string.format("%.1fM", bps / 1000000) end
end

function tick()
    local rx, tx = get_bytes()
    if not rx then return "?" end
    local now = os.clock()
    if prev_rx == 0 then
        prev_rx, prev_tx, prev_time = rx, tx, now
        return "↓0 ↑0"
    end
    local dt = now - prev_time
    if dt <= 0 then dt = 1 end
    local rx_rate = (rx - prev_rx) / dt
    local tx_rate = (tx - prev_tx) / dt
    prev_rx, prev_tx, prev_time = rx, tx, now
    return "↓" .. fmt(rx_rate) .. " ↑" .. fmt(tx_rate)
end

function on_tooltip()
    local f = io.popen("ip -br addr 2>/dev/null | grep -v lo || true", "r")
    if not f then return "" end
    local r = f:read("*a")
    f:close()
    return r
end
