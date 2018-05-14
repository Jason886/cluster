log   = ngx.log
ERR   = ngx.ERR

cjson  = require("cjson.safe")

cjson.encode_empty_table_as_object(false)
