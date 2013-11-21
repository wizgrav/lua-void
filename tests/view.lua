local void = require "void"

local v = void(16)

-- set all values to 128, default type is u8
v[0] = 128

for i,k in pairs({"u8","s8","u16","s16","u32","s32","float","double"}) do
	v.type=k
	print(k,v(1,#v))
	for j=1,#v do
		print(v[j])
	end
end

for j=2,15 do
	print("FROM: ",j)
	for i,k in pairs({"u8","s8","u16","s16","u32","s32","float","double"}) do
		v.type=k
		print(k,v(1,#v))
	end
end
