ngx.header.content_type = "application/json;charset=utf8"
ngx.header.Server       = "ChiVox-LuaSpiker"

local r = { 
    request = nil,                                     -- 客户上传的参数列表。
    data    = nil,                                     -- 当errno = 0时，为转换后的拼音码。
    error   = { errno = 1, info = 'internal error' }   -- 服务器是否出错，当errno > 0时，info会有错误信息，data值为空。
}

local chunk_size = 50 * 1024 * 1024
local form       = upload:new(chunk_size)
local params     = {}

local parse_post_header = function(line)
    local name
    local filename

    local iter, err = ngx.re.gmatch(line, "=\"([a-z|.]+)\"")
    if iter then
        local m, err = iter()
        if m then
            name = m[1]
        end

        m, err = iter()
        if m then
            filename = m[1]
        end
    else
        log(ERR, "generate iter error: ", err)
    end

    return name, filename
end

if form then
    local sock = ngx.socket.tcp()
    sock:settimeout(1000 * 300)

    local content = nil
    local name = nil
    
    while true do
        local typ, res, err = form:read()
        if not typ then
            log(ERR, "failed to read: " .. err)
            return 
        end
    
        if typ == 'header' then
            local n, f = parse_post_header(res[2])
            if n then
                name     = n
                filename = f
                
                if not filename and name == 'file' then
                    r.error.errno, r.error.info = 11, 'The param "file" is not allowed as a generic parameter unless it is used as an upload file\'s param.'

                    local json = cjson.encode(r)
                    ngx.say(json)
                    ngx.exit(ngx.HTTP_OK)
                end
            end
        elseif typ == 'body' then
            content = res
        elseif typ == 'part_end' then
            if filename then
                params['file'] = { name = name, filename = filename , data = content }
                log(ERR, string.format("recved file: %s, length: %d", filename, #content))
            else
                params[name] = { name = name , value = content }
            end
            
            content, name, filename = nil, nil, nil
        elseif typ == 'eof' then
            break
        else
            log(ERR, "unknown type: " .. typ)
        end
    end
    
    local b_params = cjson.encode(params)

    --log(ERR, "recved params: ", b_params)

    local ok, err = sock:connect("unix:/tmp/proxy-cpunodes.sock")
    if ok then
        local bytes, err = sock:send(b_params)
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
else
    log(ERR, "form: ", form)
    r.error.errno, r.error.info = 4, 'form is null'
end

if params.file then
    params.file.data = nil
end

r.request = params

local json = cjson.encode(r)
ngx.say(json)
ngx.exit(ngx.HTTP_OK)
