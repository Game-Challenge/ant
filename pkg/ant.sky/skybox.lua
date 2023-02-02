local ecs 	= ...
local world = ecs.world
local w 	= world.w

local imesh		= ecs.import.interface "ant.asset|imesh"
local geopkg 	= import_package "ant.geometry"
local geo 		= geopkg.geometry

local math3d	= require "math3d"

local skybox_sys = ecs.system "skybox_system"

function skybox_sys:component_init()
	for e in w:select "INIT skybox:in simplemesh:out owned_mesh_buffer?out skybox_changed?out" do
		local vb, ib = geo.box(1, true, false)
		local m = imesh.init_mesh{
			vb = {
				start = 0,
				num = 8,
				declname = "p3",
				memory = {"fff", vb},
				owned = true,
			},
			ib = {
				start = 0,
				num = #ib,
				memory = {"w", ib},
				owned = true,
			}
		}
		e.simplemesh = m
		e.owned_mesh_buffer = true
		e.skybox_changed = true
	end
end