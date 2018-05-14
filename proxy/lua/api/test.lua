ngx.header.content_type = "application/json;charset=utf8"
ngx.header.Server       = "ChiVox-LuaSpiker"

local r = { 
    request = req,                                     -- 客户上传的参数列表。
    data    = nil,                                     -- 当errno = 0时，为转换后的拼音码。
    error   = { errno = 1, info = 'internal error' }   -- 服务器是否出错，当errno > 0时，info会有错误信息，data值为空。
}
log(ERR, "hello, world")

local sock = ngx.socket.tcp()
sock:settimeout(5000)

local ok, err = sock:connect("unix:/tmp/proxy-cpunodes.sock")
if ok then
    local bytes, err = sock:send('hi, cpunode')
    local line, err, partial = sock:receive('*l')

    if line then
        r.data = line
        r.error.errno, r.error.info = 0, ''
    else
        log(ERR, "failed to recv msg. error: " .. err)
        r.error.errno, r.error.info = 3, err
    end
else
    log(ERR, "failed to connect to proxy stream. error: " .. err)
    r.error.errno, r.error.info = 2, err
end

local json = cjson.encode(r)
ngx.say(json)
ngx.exit(ngx.HTTP_OK)
