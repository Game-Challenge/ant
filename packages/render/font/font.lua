local ecs = ...
local world = ecs.world

local bgfx = require "bgfx"
local bgfxfont = require "bgfx.font"
local math3d = require "math3d"
local platform = require "platform"

local declmgr = require "vertexdecl_mgr"

local MAX_QUAD<const>       = 256
local MAX_VERTICES<const>   = MAX_QUAD * 4

local function create_font_texture2d()
    local s = bgfxfont.fonttexture_size
    return bgfx.create_texture2d(s, s, false, 1, "A8")
end

local fonttex_handle= create_font_texture2d()
local fonttex = {stage=0, texture={handle=fonttex_handle}}
local layout_desc   = declmgr.correct_layout "p20nii|t20nii|c40niu"
local fontquad_layout = declmgr.get(layout_desc)
local declformat    = declmgr.vertex_desc_str(layout_desc)
local tb            = bgfx.transient_buffer(declformat)
local tboffset      = 0

local imaterial = world:interface "ant.asset|imaterial"

local function alloc(n, decl)
    tb:alloc(n, decl)
    local start = tboffset
    tboffset = tboffset + n
    return start, tboffset
end

local function reset()
    tboffset = 0
end

local function create_ib()
    local ib = {}
    for i=1, MAX_QUAD do
        local offset = (i-1) * 4
        ib[#ib+1] = offset + 0
        ib[#ib+1] = offset + 1
        ib[#ib+1] = offset + 2

        ib[#ib+1] = offset + 1
        ib[#ib+1] = offset + 3
        ib[#ib+1] = offset + 2
    end
    return bgfx.create_index_buffer(bgfx.memory_buffer('w', ib), "")
end
local ibhandle = create_ib()

local irq = world:interface "ant.render|irenderqueue"
local function calc_screen_pos(pos3d, queueeid)
    queueeid = queueeid or world:singleton_entity_id "main_queue"

    local q = world[queueeid]
    local vp = world[q.camera_eid]._rendercache.viewprojmat
    local posNDC = math3d.transformH(vp, pos3d)

    local mask<const>, offset<const> = {0.5, 0.5, 1, 1}, {0.5, 0.5, 0, 0}
    local posClamp = math3d.muladd(posNDC, mask, offset)
    local vr = irq.view_rect(queueeid)

    local posScreen = math3d.tovalue(math3d.mul(math3d.vector(vr.w, vr.h, 1, 1), posClamp))

    if not math3d.origin_bottom_left then
        posScreen[2] = vr.h - posScreen[2]
    end

    return posScreen
end

local ifontmgr = ecs.interface "ifontmgr"
local allfont = {}
function ifontmgr.add_font(fontname)
    local fontid = allfont[fontname]
    if fontid == nil then
        fontid = bgfxfont.addfont(platform.font(fontname))
        allfont[fontname] = fontid
    end

    return fontid
end

local function text_start_pos(textw, texth, screenpos)
    return screenpos[1] - textw * 0.5, screenpos[2] - texth * 0.5
end

function ifontmgr.add_text3d(pos3d, fontid, text, size, color, style, queueeid)
    local screenpos = calc_screen_pos(pos3d, queueeid)
    local textw, texth, numchar = bgfxfont.prepare_text(fonttex_handle, text, size, fontid)

    local x, y = text_start_pos(textw, texth, screenpos)
    local start, num = alloc(numchar * 4, fontquad_layout.handle)
    bgfxfont.load_text_quad(tb, text, x, y, size, color, fontid)
    return start, num
end

local fontcomp = ecs.component "font"
function fontcomp:init()
    self.id = ifontmgr.add_font(self.name)
    return self
end

local fontmesh = ecs.transform "font_mesh"
function fontmesh.process_prefab(e)
    e.mesh = world.component "mesh" {
        vb = {
            start = 0,
            num = 0,
            tb,
        },
        ib = {
            start = 0,
            num = 0,
            handle = ibhandle
        }
    }
end

local fontsys = ecs.system "font_system"

local function calc_pos(e, cfg)
    if cfg.location == "header" then
        local mask<const> = {0, 1, 0, 0}
        local attacheid = e._rendercache.attach_eid
        local attach_e = world[attacheid]
        if attach_e then
            local aabb = attach_e._rendercache.aabb
            if aabb then
                local center, extent = math3d.aabb_center_extents(aabb)
                return math3d.muladd(mask, extent, center)
            end
        end
    else
        error(("not support location:%s"):format(cfg.location))
    end
end

local function draw_text3d(e, font, pos, text)
    local rc = e._rendercache
    local vb, ib = rc.vb, rc.ib

    vb.start, vb.num = ifontmgr.add_text3d(pos, font.id, text, font.size, 0xffafafaf, 0)
    ib.start, ib.num = 0, (vb.num / 4) * 2 * 3
end

local function submit_text(eid)
    local e = world[eid]
    local font = e.font
    local sc = e.show_config
    local pos = calc_pos(e, sc)

    draw_text3d(e, font, pos, sc.description)
    imaterial.set_property(eid, "s_texFont", fonttex)
end

function fontsys:camera_usage()
    for _, eid in world:each "show_config" do
        submit_text(eid)
    end
end

function fontsys:end_frame()
    reset()
end

local sn_a = ecs.action "show_name"
function sn_a.init(prefab, idx, value)
    local e = world[prefab[idx]]
    e._rendercache.attach_eid = prefab[value]
end