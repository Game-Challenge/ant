local datalist = require 'datalist'

local world
local pool
local out
local stack
local typeinfo
local out1, out2, out3

local function sortpairs(t)
    local sort = {}
    for k in pairs(t) do
        if type(k) == "string" then
            sort[#sort+1] = k
        end
    end
    table.sort(sort)
    local n = 1
    return function ()
        local k = sort[n]
        if k == nil then
            return
        end
        n = n + 1
        return k, t[k]
    end
end

local function convertreal(v)
    local g = ('%.16g'):format(v)
    if tonumber(g) == v then
        return g
    end
    return ('%.17g'):format(v)
end

local function stringify_basetype(name, v)
    if name == 'int' then
        return ('%d'):format(v)
    elseif name == 'real' then
        return convertreal(v)
    elseif name == 'string' then
        return datalist.quote(v)
    elseif name == 'boolean' then
        if v then
            return 'true'
        else
            return 'false'
        end
    elseif name == 'entityid' then
        return '[entity,'..(v == '' and '""' or v)..']'
    end
    assert('unknown base type:'..name)
end

local function stringify_array_value(c, array, v, load)
    if not load and c.method and c.method.init then
        load = c.name
    end
    if c.type ~= 'primtype' then
        return stringify_array_value(typeinfo[c.type], array, v, load)
    end
    local n = array == 0 and #v or array
    local s = {}
    for i = 1, n do
        s[i] = stringify_basetype(c.name, v[i])
    end
    if load then
        if load == 'vector' or load == 'matrix' then
            return '['..table.concat(s, ',')..']'
        end
        return '['..load..',{'..table.concat(s, ',')..'}]'
    end
    return '{'..table.concat(s, ',')..'}'
end

local function stringify_map_value(c, v, load)
    if not load and c.method and c.method.init then
        load = c.name
    end
    if c.type ~= 'primtype' then
        return stringify_map_value(typeinfo[c.type], v, load)
    end
    local s = {}
    for k, o in sortpairs(v) do
        s[#s+1] = k..':'..stringify_basetype(c.name, o)
    end
    if load then
        return '['..load..',{'..table.concat(s, ',')..'}]'
    end
    return '{'..table.concat(s, ',')..'}'
end

local function stringify_value(c, v, load)
    assert(c.type)
    if c.array then
        return stringify_array_value(c, c.array, v, load)
    end
    if c.map then
        return stringify_map_value(c, v, load)
    end
    if c.type == 'primtype' then
        if load then
            return '['..load..','..stringify_basetype(c.name, v)..']'
        end
        return stringify_basetype(c.name, v)
    end
    if not load and c.method and c.method.init then
        load = c.name
    end
	return stringify_value(typeinfo[c.type], v, load)
end

local function stringify_component_value(name, v)
    assert(typeinfo[name], "unknown type:" .. name)
    local c = typeinfo[name]
    if not c.ref then
		return stringify_value(c, v)		
    end
    if not pool[v] then
        pool[v] = v.__id
        stack[#stack+1] = {c, v}
    end
    return ('*%x'):format(pool[v])
end

local stringify_component_ref

local function stringify_component_children(c, v)
    if not c.ref then
		return stringify_value(c, v)
    end
    if c.array then
        local n = c.array == 0 and #v or c.array
        for i = 1, n do
            out[#out+1] = ('  --- %s'):format(stringify_component_value(typeinfo[c.type].name, v[i]))
        end
        return
    end
    if c.map then
        for k, o in sortpairs(v) do
            out[#out+1] = ('  %s:%s'):format(k, stringify_component_value(typeinfo[c.type].name, o))
        end
        return
    end
	return stringify_component_value(c.type, v)
end

local function is_empty_table(t)
    local k = next(t)
    if k then
        return next(t, k) == nil
    end
    
    return true
end

function stringify_component_ref(c, v, lv)
    if c.type then
        return stringify_component_ref(typeinfo[c.type], v, lv)
    end
    for _, cv in ipairs(c) do
        if v[cv.name] == nil and cv.attrib and cv.attrib.opt then
            goto continue
        end
        if cv.ref and (cv.array or cv.map) then
            local thisline = ('  '):rep(lv) .. ('%s:'):format(cv.name)
            
            local vv = v[cv.name]
            if cv.map and is_empty_table(vv) then
                out[#out+1] = thisline .. '{}'
            else
                out[#out+1] = thisline
                stringify_component_children(cv, vv)
            end
        else
            out[#out+1] = ('  '):rep(lv) .. ('%s:%s'):format(cv.name, stringify_component_children(cv, v[cv.name]))
        end
        ::continue::
    end
    for i, vv in ipairs(v) do
        if not pool[vv] then
            pool[vv] = vv.__id
            stack[#stack+1] = {c, vv}
        end
        out[#out+1] = ('  '):rep(lv) .. ('%d:*%x'):format(i, vv.__id)
    end
end

local function _stringify_entity(e)
    out[#out+1] = ('--- &%x'):format(e.__id)
    for _, c in ipairs(e) do
        local k, v = c[1], c[2]
        local ti = typeinfo[k]
        if ti.multiple then
            for _, vv in ipairs(v) do
                out[#out+1] = ('%s:%s'):format(k, stringify_component_value(k, vv))
            end
        else
            out[#out+1] = ('%s:%s'):format(k, stringify_component_value(k, v))
        end
    end

    while #stack ~= 0 do
        local c, v = stack[1][1], stack[1][2]
        table.remove(stack, 1)

        out[#out+1] = ('--- &%x'):format(pool[v])
        stringify_component_ref(c, v, 0)
    end
end

local function stringify_start(w)
    world = w
    pool = {}
    stack = {}
    typeinfo = w._class.component
    out1, out2, out3 = {}, {}, {}
end

local function stringify_package(t)
    out = out1
    out[#out+1] = '---'
    for _, name in ipairs(t[1]) do
        out[#out+1] = ('  --- %s'):format(name)
    end
end

local function stringify_end(t)
    out = out2
    out[#out+1] = '---'
    for _, cs in ipairs(t[3]) do
        out[#out+1] = '  ---'
        out[#out+1] = ('    --- %s'):format(cs[1])
        local l = {}
        for _, v in ipairs(cs[2]) do
            l[#l+1] = pool[v]
        end
        table.sort(l)
        for _, v in ipairs(l) do
            out[#out+1] = ('    --- *%x'):format(v)
        end
    end

    table.move(out2, 1, #out2, #out1+1, out1)
    table.move(out3, 1, #out3, #out1+1, out1)
    out1[#out1+1] = ''
    return table.concat(out1, '\n')
end

local function stringify_world(w, t)
    stringify_start(w)
    stringify_package(t)

    local entity = t[2]

    out = out1
    out[#out+1] = '---'
    for _, e in ipairs(entity) do
        out[#out+1] = ('  --- *%x'):format(e.__id)
    end

    out = out3
    for _, e in ipairs(entity) do
        _stringify_entity(e)
    end

    return stringify_end(t)
end

local function stringify_entity(w, t)
    stringify_start(w)
    stringify_package(t)

    local e = t[2]
    out = out1
    out[#out+1] = '---'
    out[#out+1] = ('  --- *%x'):format(e.__id)

    out = out3
    _stringify_entity(e)
    return stringify_end(t)
end

return {
    world = stringify_world,
    entity = stringify_entity,
}
