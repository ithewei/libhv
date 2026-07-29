-- hv.http coroutine-synchronous client demo.
-- Usage: hvlua examples/lua/http_client.lua [url]
local url = arg[1] or "http://127.0.0.1:18090/ping"

hv.setTimeout(1, function()
    local resp, err = hv.http.get(url)
    if err then
        hv.loge("GET failed:", err)
    else
        hv.log("GET", url, "->", resp.status, "body:", resp.body)
    end

    -- a second request on the same client/loop
    local r2, e2 = hv.http.get(url)
    if not e2 then hv.log("2nd GET ->", r2.status) end

    hv.stop()
end)
