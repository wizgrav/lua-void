local lanes = require "lanes".configure()

local function tf(id)
	local void = require "void"
	local link = void.link
	local v = void(16)
	for i=1,10 do
		v[0] = "Message "..id.."-"..i
		local ok = link(v,true)
		if #v then 
			print("Thread "..id.." got "..v()) 
		else 
			v=void(16) 
			print("Thread "..id.." view was neutered")
		end
	end
end

local t1 = lanes.gen("*",tf)("A")
local t2 = lanes.gen("*",tf)("B")

t1:join()
t2:join()
