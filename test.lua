json = require("json")

request = {
  uri = nil,
  headers = {}
}

function string.interpolate(str, vars)
  if not vars then
    vars = {}
  end
  return (
    string.gsub(str, "({{([^}]+)}})",
      function(whole,i)
        if vars[i] then
          return vars[i]
        end
        local f = loadstring(i)
        return f()
      end
    )
  )
end

function string.explode(str, sep)
  local t = {}
  for s in string.gmatch(str, "([^" .. sep .. "]+)") do
    t[#t+1] = s
  end
  return t
end

function string.trim(s)
  return s:gsub("^%s+", ""):gsub("%s+$", "")
end

while true do
  local line = io.read()
  if line == nil or line:len() <= 1 then break end

  local line = line:trim()

  if line:match("^GET") then
    request.uri = line:sub(4):match("%g+")
  else
    request.headers[line:match("^[^:]+")] = line:match(":.+$"):sub(3)
  end
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
