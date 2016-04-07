-- HTTP

request = {
  sock = sock,
  uri = nil,
  headers = {}
}

json = require("json")

function string.fsub(str)
  return string.gsub(str, "({{([^}]+)}})",
    function(whole,i)
      return loadstring(i)()
    end)
end

function string.ltrim(s)
  return s:gsub("^%s+", "")
end

function string.rtrim(s)
  return s:gsub("%s+$", "")
end

function string.trim(s)
  return s:ltrim():rtrim()
end

function print(s)
  request.sock:write(s .. "\n")
end

local line = request.sock:read():trim()
request.uri = line:sub(line:find("%s")+1):match("%g+")

while true do
  local line = request.sock:read()
  if line == nil then break end

  local line = line:trim()
  if line:len() == 0 then break end

  request.headers[line:match("^[^:]+")] = line:match(":.+$"):sub(3)
end

if request.uri:match("[.][^.]+$") then
  print("HTTP/1.0 404 Not Found")
  print("")
  return
end

print("HTTP/1.0 200 OK")
print("Content-Type: text/plain")
print("")

print("uri: " .. request.uri)

for k,v in pairs(request.headers) do
  print(k .. " => " .. v)
end
