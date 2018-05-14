ngx.header.content_type = "application/json;charset=utf8"
ngx.header.Server       = "ChiVox-LuaSpiker"

local r = { 
    request = nil,                                       -- 客户上传的参数列表。
    data    = nil,                                       -- 当errno = 0时，为转换后的拼音码。
    error   = { errno = 502, info = 'internal error' }   -- 服务器是否出错，当errno > 0时，info会有错误信息，data值为空。
}

ngx.req.read_body()
local args, err = ngx.req.get_post_args()
if not args then
    r.error.errno = 101
    log(ERR, 'errno: ', r.error.errno, ', detail: ', err)
else
    for k, v in pairs(args) do
        if type(v) == 'table' then
            log(ERR, k, ': ', table.concat(v, ', '))
        else
            log(ERR, k, ': ', v)
        end
    end

    r.request = args
end

local json = cjson.encode(r)
ngx.say(json)
ngx.exit(ngx.HTTP_OK)
