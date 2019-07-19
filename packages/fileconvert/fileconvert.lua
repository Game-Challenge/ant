local lfs = require "filesystem.local"

local converter_names = {
	shader = "shader.compile",
	mesh = "mesh.convert",
	texture = "texture.convert",
}

local function rawtable(filepath)
	local env = {}
	local r = assert(lfs.loadfile(filepath, "t", env))
	r()
	return env
end


local logfolder = lfs.current_path() / "log"
lfs.create_directories(logfolder)

local logfile = nil

local function get_logfile()
	if logfile == nil then
		logfile = assert(lfs.open(logfolder / "fileconvert.log", "a"))
	end

	return logfile
end

local origin = os.time() - os.clock()
local function os_date()
    local ti, tf = math.modf(origin + os.clock())
    return os.date('%Y-%m-%d %H:%M:%S:{ms}', ti):gsub('{ms}', math.floor(tf*1000))
end

local function log_err(src, lk, err)
	local log = get_logfile()
	local errinfo = string.format("[fileconvert:%s]src:%s, lk:%s, error:%s\n", os_date(), src, lk, err)
	log:write(errinfo)
	log:flush()
	print(errinfo)
end

local function log_info(info)
	local log = get_logfile()
	log:write(string.format("[fileconvert-info:%s]%s\n", os_date(), info))
	log:flush()
end

return function (plat, sourcefile, lkfile, dstfile)
	local lkcontent = rawtable(lkfile)
	local ctype = assert(lkcontent.type)
	local converter_name = assert(converter_names[ctype])

	local c = require(converter_name)
	log_info(string.format("plat:%s, src:%s, lk:%s, dst:%s, cvt type:%s", plat, sourcefile, lkfile, dstfile, ctype))
	local success, err = c(plat, sourcefile, lkcontent, dstfile)
	if not success and err then
		log_err(sourcefile, lkfile, err)
	end

	return success
end