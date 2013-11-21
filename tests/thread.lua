local void = require "void"
local lanes = require "lanes".configure()

local function tf(id)
	local void = require "void"
	local link = void.link
	while true do
		local v = link[0]
		if not v then break end
		print("Thread "..id.." "..v())
	end
end

local link = void.link
for i=1,10 do
	link[10] = "got "..i
end
local t1 = lanes.gen("*",tf)(1)
local t2 = lanes.gen("*",tf)(2)

t1:join()
t2:join()
