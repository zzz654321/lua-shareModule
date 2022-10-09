local function css2tbl(css ) 
	local ret_, i, j, t= {}, 1, 1, io.open(css, "rb" ) 
	css= ("\n".. t:read("*a" ) ):gsub("\r", "" ) t:close() 
	while j do j= css:find("}", i ) if not j then break end 
		t= css:find("{", i ) ret_[#ret_+ 1 ]= 
			{ css:sub(i, t- 1 ):gsub("\n", "" ), css:sub(t+ 1, j- 1 ) 
					:gsub("([;{])\n +", "%1" ):gsub("\n}", "}" ), 
			} i= j+ 1 
	end return ret_ 
end 
local function cmpFunc(i)return function(a,b)return a[i]<b[i]end end 
local function css_tidy1(tbl ) local css, t= {}, 1 
	local function F_(i ) local t1= {} for j= t, i- 1 do 
			for i in ipairs(t1 ) do 
				if t1[i]== tbl[j][2] then tbl[j][2]= "" end 
			end t1[#t1+ 1 ]= tbl[j][2] 
		end t1= table.concat(t1 ) css[#css+ 1 ]= 
			{ tbl[t][1], t1:gsub(";\n\n", ";\n" ), } 
	end table.sort(tbl, cmpFunc(1 ) ) 
	for i, j in ipairs(tbl ) do 
		if j[1]~= tbl[t][1] then F_(i ) t= i end 
	end F_(#tbl+ 1 ) return css 
end 
local function css_compress1(tbl ) local css, t, t1, t2= {}, 1 
	table.sort(tbl, cmpFunc(2 ) ) 
	for i, j in ipairs(tbl ) do if false then 
		elseif j[1]:find("type='radio'" ) then --特殊处理
			css[#css+ 1 ]= { j[1], j[2], } j[1]= nil 
		elseif j[2]~= tbl[t][2] then 
			t1= {} for j= t, i- 1 do t1[#t1 + 1 ]= tbl[j][1] end 
			j= t t= i css[#css+ 1 ]= ((0< #t1 ) and 
					{ table.concat(t1, ", " ), tbl[j][2], } or nil ) 
		end 
	end table.sort(css, cmpFunc(1 ) ) for i, j in ipairs(css ) do 
			if "body"== j[1] then t2= table.remove(css, i ) end end 
	if t2 then table.insert(css, 1, t2 ) end 
	return css 
end 

local f, tbl= "authList" --"authSchedule" --"authDetail" --"T1" --
tbl= css2tbl(f .."-o.css" ) tbl= css_tidy1(tbl ) tbl= css_compress1(tbl ) 
for i, j in pairs(tbl ) do tbl[i]= j[1] .."{".. j[2] .."}" end 
f= io.open("x:/TEMP/".. f ..".css", "wb" ) 
f:write(table.concat(tbl, "\n" ) ) f:close() 

