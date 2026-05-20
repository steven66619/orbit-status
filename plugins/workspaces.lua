interval = 1

function tick()
    local ws = get_xmonad_workspaces()
    local count = 0
    for _ in pairs(ws) do
        count = count + 1
    end
    if count == 0 then
        return ""
    end
    local parts = {}
    for i = 1, count do
        parts[i] = "[" .. ws[i] .. "]"
    end
    return table.concat(parts, " ")
end
