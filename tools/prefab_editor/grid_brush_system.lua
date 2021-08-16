local ecs = ...
local world = ecs.world
local mathutils = require "mathutils"(world)
local camera_mgr = require "camera_manager"(world)
local gridmesh_view = require "widget.gridmesh_view"(world)
local widget_utils  = require "widget.utils"
local math3d = require "math3d"
local iom = world:interface "ant.objcontroller|obj_motion"
local iss = world:interface "ant.scene|iscenespace"
local computil = world:interface "ant.render|entity"
local utils     = require "common.utils"
local brush_sys = ecs.system "grid_brush_system"
local global_data = require "common.global_data"
local brush_def = require "brush_def"
--local default_color = {1.0, 1.0, 1.0, 0.5}
local current_brush_color = {1, 1, 1, 0.5}
local current_brush_id
local grid = {
    brush = {}
}

local function color_clamp(c)
    if c < 0 then return 0 end
    if c > 255 then return 255 end
    return c
end


local function colorf2i(fc)
    return color_clamp(math.floor(fc[1] * 255.0)) << 24 | color_clamp(math.floor(fc[2] * 255.0)) << 16 | color_clamp(math.floor(fc[3] * 255.0)) << 8 | color_clamp(math.floor(fc[4] * 255.0))
end

local function colori2f(ic)
    return {((ic & 0xFF000000) >> 24) / 255.0, ((ic & 0x00FF0000) >> 16) / 255.0, ((ic & 0x0000FF00) >> 8) / 255.0, (ic & 0xFF) / 255.0 }
end

local function fromhex(str)
    return (str:gsub('..', function (cc)
        return string.char(tonumber(cc, 16))
    end))
end

local function tohex(str)
    return (str:gsub('.', function (c)
        return string.format('%02X', string.byte(c))
    end))
end

local function get_brush_id(color)
    for i, c in ipairs(grid.brush) do
        if color == c then
            return i
        end
    end
    grid.brush[#grid.brush + 1] = color
    return #grid.brush
end

function grid:clear()
    if self.data and #self.data > 0 then
        for i = 1, self.row do
            for j = 1, self.col do
                world:remove_entity(self.data[i][j].eid)
            end
        end
    end
end

function grid:init(size, row, col)
    self:clear()
    self.size = size
    self.row = row
    self.col = col
    self.total_width = size * col
    self.total_height = size * row
    self.data = {}
    self.visible = true
    local default_color = colori2f(brush_def.color[1])
    for i = 1, row do
        local temp_row = {}
        local posz = 0.5 * size - self.total_width * 0.5 + (i - 1) * size
        for j = 1, col do
            local posx = 0.5 * size - self.total_height * 0.5 + (j  - 1) * size
            local tile_eid = computil.create_prim_plane_entity({t = {posx, 0, posz, 1}, s = {size * 0.95, 0, size * 0.95, 0}},
		                "/pkg/ant.resources/materials/singlecolor_translucent_nocull.material", "grid", true)
	        ies.set_state(tile_eid, "auxgeom", true)
	        imaterial.set_property(tile_eid, "u_color", default_color)
            temp_row[#temp_row + 1] = {1, eid = tile_eid}
        end
        self.data[#self.data + 1] = temp_row
    end
end

function grid:show(show)
    self.visible = show
    for _, row in ipairs(self.data) do
        for _, tile in ipairs(row) do
            ies.set_state(tile.eid, "visible", show)
        end
    end
end

function grid:load(path)
    local filename = string.sub(path, #global_data.project_root:string() + 2, -5)
    local source = require(string.gsub(filename, "/", "."))
    if not source or not source.size or not source.row or not source.col then return end
    for i = 1, source.row do
        local posz = 0.5 * source.size - source.total_width * 0.5 + (i - 1) * source.size
        for j = 1, source.col do
            local tile = source.data[i][j]
            local posx = 0.5 * source.size - source.total_height * 0.5 + (j  - 1) * source.size
            local eid = computil.create_prim_plane_entity({t = {posx, 0, posz, 1}, s = {source.size * 0.95, 0, source.size * 0.95, 0}},
		                "/pkg/ant.resources/materials/singlecolor_translucent_nocull.material", "grid", true)
	        ies.set_state(eid, "auxgeom", true)
	        imaterial.set_property(eid, "u_color", colori2f(self.brush[tile[1]]))
            tile.eid = eid
        end
    end
    self:clear()
    self.size = source.size
    self.row = source.row
    self.col = source.col
    self.total_width = source.total_width
    self.total_height = source.total_height
    self.data = source.data
    self.filename = path
    self.visible = true
end

function grid:save(filename)
    if not filename then
        local newfilename = widget_utils.get_saveas_path("Lua", ".lua")
        if not newfilename then return end
        self.filename = newfilename
    end

    local temp = utils.deep_copy(self)
    temp.filename = nil
    temp.show = nil
    temp.save = nil
    temp.init = nil
    temp.clear = nil
    temp.load = nil
    temp.brush = nil
    for _, row in ipairs(temp.data) do
        for _, tile in ipairs(row) do
            tile.eid = nil
            --tile.color = string.format("%02X", color_clamp(math.floor(tile.color[1] * 255.0)) << 24 | color_clamp(math.floor(tile.color[2] * 255.0)) << 16 | color_clamp(math.floor(tile.color[3] * 255.0)) << 8 | color_clamp(math.floor(tile.color[4] * 255.0)))
        end
    end
    utils.write_file(self.filename, "return " .. utils.table_to_string(temp))
end

function brush_sys:init(size, row, col)
    gridmesh_view.set_grid(grid)
end

local function get_row_col(sx, sy)
    local rawpos = mathutils.ray_hit_plane(iom.ray(camera_mgr.main_camera, {sx, sy}), {dir = {0,1,0}, pos = {0,0,0}})
    if not rawpos then
        return
    end
    local pos = math3d.totable(rawpos)
    local min_x, min_y = -0.5 * grid.total_width, -0.5 * grid.total_height
    local max_x, max_y = 0.5 * grid.total_width, 0.5 * grid.total_height
    if pos[1] < min_x or pos[1] > max_x or pos[3] < min_y or pos[3] > max_y then
        return
    else
        return math.floor((pos[3] - min_y) / grid.size) + 1, math.floor((pos[1] - min_x) / grid.size) + 1
    end
end

local last_row, last_col
local function on_row_col_select(row, col)
    if not row or not col or (last_row==row and last_col==col) then return end
    local tile = grid.data[row][col]
    tile[1] = current_brush_id
    imaterial.set_property(tile.eid, "u_color", current_brush_color)
    last_row, last_col = row, col
end

local event_keyboard = world:sub{"keyboard"}
local event_mouse_down = world:sub {"mousedown"}
local event_mouseup = world:sub {"mouseup"}
local event_mouse_drag = world:sub {"mousedrag"}
local event_gridmesh = world:sub {"GridMesh"}

local event_reset = world:sub {"ResetEditor"}

local brush_state = false

function brush_sys:handle_event()
    if not grid then return end

    for _, what in event_reset:unpack() do
        grid:clear()
    end

    for _, what, x, y in event_mouseup:unpack() do
		if what == "LEFT" then
            last_row, last_col = nil, nil
        end
    end

    for _, what, p1, p2, p3, p4 in event_gridmesh:unpack() do
        if what == "create" then
            grid:init(p1, p2, p3)
        elseif what == "brushcolor" then
            current_brush_id = p1
            current_brush_color = colori2f(p2)--{p1, p2, p3, p4}
        elseif what == "load" then
            load_grid(p1)
        end
    end

    for _, key, press, state in event_keyboard:unpack() do
        brush_state = state.CTRL
    end

    local brush_id
    for _, what, sx, sy, dx, dy in event_mouse_drag:unpack() do
		if what == "LEFT" and brush_state then
            if not brush_id then
                --current_brush_id = get_brush_id(colorf2i(current_brush_color))
                brush_id = current_brush_id
            end
            on_row_col_select(get_row_col(sx, sy))
        end
    end
end

function brush_sys:post_init()

end