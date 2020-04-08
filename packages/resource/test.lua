local resource = require "resource"

local code = {}

local function make_resource(name, func)
	code[name] = string.dump(func)
end

local function loader(data, filename)
	print("Load", filename)
	local func = load(data)
	return func()
end

make_resource("a.code", function() return { a = 1, b = 2, c = 3 } end)

resource.register_ext("code", loader, function(tbl, filename) print("Unload", filename) end)

local function reload_code(name)
	resource.reload(name, code[name])
end

local function load_code(name, lazyload)
	resource.load(name, code[name], lazyload)
end

local proxy = resource.proxy "a.code"

assert(resource.status(proxy) == "ref")
assert(tostring(proxy) == "a.code:")

local result = {}
resource.status(proxy, result)
assert(result[1] == "a.code")

reload_code "a.code"

assert(proxy.a == 1)
assert(proxy.b == 2)
assert(proxy.c == 3)

local a = resource.proxy("a.code", "a")

make_resource("a.code", function()
	return {
		x = { 1,2,3 },
		y = { a = { b = { "hello" } } },
		z = {
			{ "hello" },
			{ "world" },
		},
	}
end)
reload_code "a.code"	-- reload

local touched = resource.monitor("a.code", true)

assert(touched() == false)

assert(resource._data == nil)	-- invalid

assert(touched() == false)

assert(proxy.x[1] == 1)

assert(touched() == true)

resource.monitor("a.code", false)	-- turn off monitor

local x = resource.proxy("a.code", "x")

assert(x[2] == 2)
assert(x[3] == 3)

local y_a = resource.proxy("a.code", "y.a")
assert(y_a.b[1] == "hello")

local z_1 = resource.proxy("a.code", "z.1")
assert(z_1[1] == "hello")

resource.unload "a.code"

assert(tostring(x) == "a.code:x")
assert(x._data == false)	-- unload
assert(resource.status(x) == "ref")

load_code "a.code"
resource.unload "a.code"
load_code ("a.code", true)	-- turn on autoload

assert(x[1] == 1)

resource.unload "a.code"

local result = {}

-- pairs trigger auto reload
for k,v in pairs(x) do
	result[k] = v
end

assert(result[1] == 1)
assert(result[2] == 2)
assert(result[3] == 3)
