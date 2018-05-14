var http = require('http')
var url = require('url')
var fs = require('fs')

var cfg = {
  "host": "10.0.200.20",
  "port": 5001,
  "site": "./site"
}

var mime = {
  ".html": "text/html;charset=utf8",
  ".txt": "text/plain",
  ".zip": "application/x-zip-compressed",
  ".rar": "application/x-rar",
  ".gz": "application/x-gzip",
  ".7z": "application/x-7z-compressed",
  ".mp3": "audio/mpeg",
  ".wav": "audio/x-wav",
  ".": 'application/octet-stream',
  "": 'application/octet-stream'
}

var getFileName = function(filePath) {
  if (filePath != null) {
    var index1 = filePath.lastIndexOf('/');
    var fileName = filePath.substring(index1+1, filePath.length);
    return fileName
  }
  return null
}

var getExt = function(fileName) {
  if (fileName != null) {
    var index1 = fileName.lastIndexOf('.');
    if (index1 >= 0) {
      var ext = fileName.substring(index1, fileName.length);
      return ext
    }
  }
  return null
}

var getMine = function(ext) {
  if (ext != null) {
    var contentType = mime[ext.toLowerCase()];
    if (contentType != null) return contentType;
  }
  return 'application/octet-stream'
}

var server = http.createServer(function(req, res) {
  var reqPath = url.parse(req.url).path;
  var filePath = cfg.site;
  if (reqPath != "/") filePath += reqPath;
  console.log("reqPath: %s -> filePath:%s", reqPath, filePath)

  if (reqPath == "/callback") {
    var buffers=[];
    req.on('data',function(trunk) {
      buffers.push(trunk);
    }).on('end',function() {
      var buffer = Buffer.concat(buffers);
      console.log("callback: " + buffer);
    })
    res.writeHead(200, {'Content-Type': 'text/json;charset=utf8'});
    res.end('{"token":"xxxxxx", "result":0}');
    return;
  }

  fs.exists(filePath, function(exists) {
    if (exists) {
      fs.stat(filePath, function(err, stats) {
        if (err) {
          console.log(err)
          res.writeHead(500, {'Content-Type': 'text/html;charset=utf8'});
          res.end('<div styel="color:black;font-size:22px;">server error</div>')
        } else {
          if (stats.isFile()) {
            var fileExt = getExt(getFileName(filePath));
            var contentType = getMine(fileExt)
            console.log("fileExt: %s, contentType: %s", fileExt != null ? fileExt : "null", contentType)
            var file = fs.createReadStream(filePath);
            res.writeHead(200, {'Content-Type': contentType});
            file.pipe(res)
          } else {
            fs.readdir(filePath, function(err, files) {
              var str = '';
              if (files.length == 0) {
                str += 'no files!!'
              } else {
                for (var i in files) {
                  str += files[i] + '<br>';
                }
              }
              res.writeHead(200, {'Content-Type':'text/html;charset=utf8'});
              res.end(str);
            });
          }
        }
      })
    } else {
      console.log("file not found: %s", filePath)
      res.writeHead(404, {'Content-Type': 'text/html;charset=utf8'});
      res.end('<div styel="color:black;font-size:22px;">404 not found</div>');
    }
  });
});

server.listen(cfg.port, cfg.host)
console.log('listen at: ' + cfg.host + ':' + cfg.port)
