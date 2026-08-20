/**************************************************************************/
/*  openxr_meta_environment_depth.cpp                                     */
/**************************************************************************/
/*                       This file is part of:                            */
/*                              GODOT XR                                  */
/*                      https://godotengine.org                           */
/**************************************************************************/
/* Copyright (c) 2022-present Godot XR contributors (see CONTRIBUTORS.md) */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "classes/openxr_meta_environment_depth.h"

#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/open_xrapi_extension.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/xr_camera3d.hpp>
#include <godot_cpp/classes/xr_interface.hpp>
#include <godot_cpp/classes/xr_server.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "extensions/openxr_meta_environment_depth_extension.h"

using namespace godot;

static const char *META_ENVIRONMENT_DEPTH_MASK_SHADER_CODE = R"(
shader_type spatial;
render_mode unshaded, fog_disabled, shadows_disabled, cull_disabled, depth_draw_always;

global uniform highp mat4 META_ENVIRONMENT_DEPTH_PROJECTION_VIEW_LEFT;
global uniform highp mat4 META_ENVIRONMENT_DEPTH_PROJECTION_VIEW_RIGHT;
global uniform highp vec2 META_ENVIRONMENT_DEPTH_Z_BUFFER_PARAMS;
uniform int mask_view_index = 0;
uniform highp float mask_filter_bias = 0.01;

void vertex() {
	highp mat4 projection_view = mask_view_index == 0 ? META_ENVIRONMENT_DEPTH_PROJECTION_VIEW_LEFT : META_ENVIRONMENT_DEPTH_PROJECTION_VIEW_RIGHT;
	highp vec4 clip_position = projection_view * MODEL_MATRIX * vec4(VERTEX, 1.0);
#if CURRENT_RENDERER != RENDERER_COMPATIBILITY
	// Keep Meta's OpenGL Y orientation because the mask is sampled with the
	// raw environment-depth UVs. Only convert conventional OpenGL depth
	// (-1..1, near to far) to Godot's Vulkan reverse-Z clip depth (1..0).
	clip_position.z = clip_position.w * 0.5 - clip_position.z * 0.5;
#endif
	// Render both eyes into one atlas. The left eye occupies the left half and
	// the right eye occupies the right half of the viewport.
	clip_position.x = clip_position.x * 0.5 + (mask_view_index == 0 ? -0.5 : 0.5) * clip_position.w;
	POSITION = clip_position;
}

vec3 encode_depth(highp float depth) {
	highp vec3 encoded = fract(clamp(depth, 0.0, 1.0 - 1.0 / 16581375.0) * vec3(1.0, 255.0, 65025.0));
	encoded -= encoded.yzz * vec3(1.0 / 255.0, 1.0 / 255.0, 0.0);
	return encoded;
}

highp float to_linear_depth(highp float non_linear_depth) {
	highp float ndc_depth = non_linear_depth * 2.0 - 1.0;
	return META_ENVIRONMENT_DEPTH_Z_BUFFER_PARAMS.x / (ndc_depth + META_ENVIRONMENT_DEPTH_Z_BUFFER_PARAMS.y);
}

highp float to_non_linear_depth(highp float linear_depth) {
	highp float ndc_depth = META_ENVIRONMENT_DEPTH_Z_BUFFER_PARAMS.x / linear_depth - META_ENVIRONMENT_DEPTH_Z_BUFFER_PARAMS.y;
	return ndc_depth * 0.5 + 0.5;
}

void fragment() {
	highp float mask_depth = FRAGCOORD.z;
#if CURRENT_RENDERER != RENDERER_COMPATIBILITY
	// Meta depth is conventional (near 0, far 1), while Godot's Vulkan depth
	// buffer is reversed (near 1, far 0).
	mask_depth = 1.0 - mask_depth;
#endif
	// Apply the bias in this low-resolution pass. The full-resolution
	// reprojection shader then only needs one texture sample and one comparison.
	highp float linear_depth = to_linear_depth(mask_depth);
	highp float biased_linear_depth = max(linear_depth - mask_filter_bias, 0.00001);
	mask_depth = to_non_linear_depth(biased_linear_depth);
	ALBEDO = encode_depth(mask_depth);
}
)";

void OpenXRMetaEnvironmentDepth::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_render_priority", "render_priority"), &OpenXRMetaEnvironmentDepth::set_render_priority);
	ClassDB::bind_method(D_METHOD("get_render_priority"), &OpenXRMetaEnvironmentDepth::get_render_priority);

	ClassDB::bind_method(D_METHOD("set_reprojection_offset_scale", "offset_scale"), &OpenXRMetaEnvironmentDepth::set_reprojection_offset_scale);
	ClassDB::bind_method(D_METHOD("get_reprojection_offset_scale"), &OpenXRMetaEnvironmentDepth::get_reprojection_offset_scale);

	ClassDB::bind_method(D_METHOD("set_reprojection_offset_exponent", "offset_exponent"), &OpenXRMetaEnvironmentDepth::set_reprojection_offset_exponent);
	ClassDB::bind_method(D_METHOD("get_reprojection_offset_exponent"), &OpenXRMetaEnvironmentDepth::get_reprojection_offset_exponent);

	ClassDB::bind_method(D_METHOD("set_bilinear_filtering", "enabled"), &OpenXRMetaEnvironmentDepth::set_bilinear_filtering);
	ClassDB::bind_method(D_METHOD("get_bilinear_filtering"), &OpenXRMetaEnvironmentDepth::get_bilinear_filtering);

	ClassDB::bind_method(D_METHOD("set_mask_filter_enabled", "enabled"), &OpenXRMetaEnvironmentDepth::set_mask_filter_enabled);
	ClassDB::bind_method(D_METHOD("get_mask_filter_enabled"), &OpenXRMetaEnvironmentDepth::get_mask_filter_enabled);
	ClassDB::bind_method(D_METHOD("set_mask_filter_bias", "bias"), &OpenXRMetaEnvironmentDepth::set_mask_filter_bias);
	ClassDB::bind_method(D_METHOD("get_mask_filter_bias"), &OpenXRMetaEnvironmentDepth::get_mask_filter_bias);
	ClassDB::bind_method(D_METHOD("set_mask_filter_resolution_scale", "scale"), &OpenXRMetaEnvironmentDepth::set_mask_filter_resolution_scale);
	ClassDB::bind_method(D_METHOD("get_mask_filter_resolution_scale"), &OpenXRMetaEnvironmentDepth::get_mask_filter_resolution_scale);
	ClassDB::bind_method(D_METHOD("set_mask_filter_update_rate", "rate"), &OpenXRMetaEnvironmentDepth::set_mask_filter_update_rate);
	ClassDB::bind_method(D_METHOD("get_mask_filter_update_rate"), &OpenXRMetaEnvironmentDepth::get_mask_filter_update_rate);
	ClassDB::bind_method(D_METHOD("add_mask_mesh", "mesh"), &OpenXRMetaEnvironmentDepth::add_mask_mesh);
	ClassDB::bind_method(D_METHOD("remove_mask_mesh", "mesh"), &OpenXRMetaEnvironmentDepth::remove_mask_mesh);
	ClassDB::bind_method(D_METHOD("clear_mask_meshes"), &OpenXRMetaEnvironmentDepth::clear_mask_meshes);
	ClassDB::bind_method(D_METHOD("get_mask_meshes"), &OpenXRMetaEnvironmentDepth::get_mask_meshes);
	ClassDB::bind_method(D_METHOD("get_mask_depth_texture", "view_index"), &OpenXRMetaEnvironmentDepth::get_mask_depth_texture);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "render_priority"), "set_render_priority", "get_render_priority");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bilinear_filtering"), "set_bilinear_filtering", "get_bilinear_filtering");

	ADD_GROUP("Mask Mesh Filter", "mask_filter_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "mask_filter_enabled"), "set_mask_filter_enabled", "get_mask_filter_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mask_filter_bias", PROPERTY_HINT_RANGE, "0.0,0.25,0.001,suffix:m"), "set_mask_filter_bias", "get_mask_filter_bias");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mask_filter_resolution_scale", PROPERTY_HINT_RANGE, "0.25,1.0,0.05"), "set_mask_filter_resolution_scale", "get_mask_filter_resolution_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mask_filter_update_rate", PROPERTY_HINT_RANGE, "1.0,90.0,1.0,suffix:Hz"), "set_mask_filter_update_rate", "get_mask_filter_update_rate");

	ADD_GROUP("Reprojection Offset", "reprojection_offset_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reprojection_offset_scale"), "set_reprojection_offset_scale", "get_reprojection_offset_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reprojection_offset_exponent"), "set_reprojection_offset_exponent", "get_reprojection_offset_exponent");
}

void OpenXRMetaEnvironmentDepth::set_render_priority(int p_render_priority) {
	OpenXRMetaEnvironmentDepthExtension::get_singleton()->set_reprojection_render_priority(p_render_priority);
}

int OpenXRMetaEnvironmentDepth::get_render_priority() const {
	return OpenXRMetaEnvironmentDepthExtension::get_singleton()->get_reprojection_render_priority();
}

void OpenXRMetaEnvironmentDepth::set_reprojection_offset_scale(float p_offset_scale) {
	OpenXRMetaEnvironmentDepthExtension::get_singleton()->set_reprojection_offset_scale(p_offset_scale);
}

float OpenXRMetaEnvironmentDepth::get_reprojection_offset_scale() const {
	return OpenXRMetaEnvironmentDepthExtension::get_singleton()->get_reprojection_offset_scale();
}

void OpenXRMetaEnvironmentDepth::set_reprojection_offset_exponent(float p_offset_exponent) {
	OpenXRMetaEnvironmentDepthExtension::get_singleton()->set_reprojection_offset_exponent(p_offset_exponent);
}

float OpenXRMetaEnvironmentDepth::get_reprojection_offset_exponent() const {
	return OpenXRMetaEnvironmentDepthExtension::get_singleton()->get_reprojection_offset_exponent();
}

void OpenXRMetaEnvironmentDepth::set_bilinear_filtering(bool p_enabled) {
	OpenXRMetaEnvironmentDepthExtension::get_singleton()->set_reprojection_bilinear_filtering(p_enabled);
}

bool OpenXRMetaEnvironmentDepth::get_bilinear_filtering() const {
	return OpenXRMetaEnvironmentDepthExtension::get_singleton()->get_reprojection_bilinear_filtering();
}

void OpenXRMetaEnvironmentDepth::set_mask_filter_enabled(bool p_enabled) {
	if (mask_filter_enabled == p_enabled) {
		return;
	}

	mask_filter_enabled = p_enabled;
	if (mask_filter_enabled && is_inside_tree() && !mask_meshes.is_empty()) {
		_ensure_mask_resources();
		_sync_mask_meshes();
	} else if (!mask_filter_enabled) {
		_free_mask_resources();
	}
	_update_mask_filter_state();
}

bool OpenXRMetaEnvironmentDepth::get_mask_filter_enabled() const {
	return mask_filter_enabled;
}

void OpenXRMetaEnvironmentDepth::set_mask_filter_bias(float p_bias) {
	mask_filter_bias = MAX(0.0f, p_bias);
	XRServer *xr_server = XRServer::get_singleton();
	mask_filter_world_scale = xr_server ? static_cast<float>(xr_server->get_world_scale()) : 1.0f;
	for (int eye = 0; eye < 2; eye++) {
		if (mask_materials[eye].is_valid()) {
			mask_materials[eye]->set_shader_parameter("mask_filter_bias", mask_filter_bias * mask_filter_world_scale);
		}
	}
	mask_viewport_dirty = true;
	_request_mask_viewport_update();
}

float OpenXRMetaEnvironmentDepth::get_mask_filter_bias() const {
	return mask_filter_bias;
}

void OpenXRMetaEnvironmentDepth::set_mask_filter_resolution_scale(float p_scale) {
	float scale = CLAMP(p_scale, 0.25f, 1.0f);
	if (Math::is_equal_approx(mask_filter_resolution_scale, scale)) {
		return;
	}

	mask_filter_resolution_scale = scale;
	_update_mask_viewport_size();
	mask_viewport_dirty = true;
	_request_mask_viewport_update();
}

float OpenXRMetaEnvironmentDepth::get_mask_filter_resolution_scale() const {
	return mask_filter_resolution_scale;
}

void OpenXRMetaEnvironmentDepth::set_mask_filter_update_rate(float p_rate) {
	float rate = CLAMP(p_rate, 1.0f, 90.0f);
	if (Math::is_equal_approx(mask_filter_update_rate, rate)) {
		return;
	}

	mask_filter_update_rate = rate;
	mask_viewport_dirty = true;
	_request_mask_viewport_update();
}

float OpenXRMetaEnvironmentDepth::get_mask_filter_update_rate() const {
	return mask_filter_update_rate;
}

void OpenXRMetaEnvironmentDepth::add_mask_mesh(MeshInstance3D *p_mesh) {
	ERR_FAIL_NULL(p_mesh);
	ObjectID source_id(p_mesh->get_instance_id());
	for (const MaskMeshEntry &entry : mask_meshes) {
		if (entry.source_id == source_id) {
			return;
		}
	}

	MaskMeshEntry entry;
	entry.source_id = source_id;
	mask_meshes.push_back(entry);

	if (mask_filter_enabled && is_inside_tree()) {
		_ensure_mask_resources();
		_create_mask_instances(mask_meshes[mask_meshes.size() - 1]);
		_sync_mask_meshes();
	}
	_update_mask_filter_state();
}

void OpenXRMetaEnvironmentDepth::remove_mask_mesh(MeshInstance3D *p_mesh) {
	ERR_FAIL_NULL(p_mesh);
	ObjectID source_id(p_mesh->get_instance_id());
	for (uint32_t i = 0; i < mask_meshes.size(); i++) {
		if (mask_meshes[i].source_id == source_id) {
			_free_mask_instances(mask_meshes[i]);
			mask_meshes.remove_at(i);
			break;
		}
	}

	if (mask_meshes.is_empty()) {
		_free_mask_resources();
	}
	_update_mask_filter_state();
}

void OpenXRMetaEnvironmentDepth::clear_mask_meshes() {
	for (MaskMeshEntry &entry : mask_meshes) {
		_free_mask_instances(entry);
	}
	mask_meshes.clear();
	_free_mask_resources();
	_update_mask_filter_state();
}

Array OpenXRMetaEnvironmentDepth::get_mask_meshes() const {
	Array result;
	for (const MaskMeshEntry &entry : mask_meshes) {
		MeshInstance3D *mesh = Object::cast_to<MeshInstance3D>(ObjectDB::get_instance(entry.source_id));
		if (mesh) {
			result.push_back(mesh);
		}
	}
	return result;
}

RID OpenXRMetaEnvironmentDepth::get_mask_depth_texture(int p_view_index) const {
	ERR_FAIL_INDEX_V(p_view_index, 2, RID());
	if (!mask_viewport.is_valid()) {
		return RID();
	}

	RenderingServer *rs = RenderingServer::get_singleton();
	ERR_FAIL_NULL_V(rs, RID());
	return rs->viewport_get_texture(mask_viewport);
}

void OpenXRMetaEnvironmentDepth::_ensure_mask_resources() {
	if (mask_viewport.is_valid()) {
		return;
	}

	RenderingServer *rs = RenderingServer::get_singleton();
	ERR_FAIL_NULL(rs);

	mask_shader.instantiate();
	mask_shader->set_code(META_ENVIRONMENT_DEPTH_MASK_SHADER_CODE);
	XRServer *xr_server = XRServer::get_singleton();
	mask_filter_world_scale = xr_server ? static_cast<float>(xr_server->get_world_scale()) : 1.0f;

	mask_environment = rs->environment_create();
	rs->environment_set_background(mask_environment, RenderingServer::ENV_BG_COLOR);
	// An empty mask pixel represents the far plane. This avoids relying on the
	// viewport alpha channel, which is not guaranteed to survive the Mobile 3D
	// render path, and matches Meta's depth mask preprocessing convention.
	rs->environment_set_bg_color(mask_environment, Color(1.0, 1.0, 1.0, 1.0));
	rs->environment_set_ambient_light(mask_environment, Color(0.0, 0.0, 0.0), RenderingServer::ENV_AMBIENT_SOURCE_DISABLED, 0.0, 0.0, RenderingServer::ENV_REFLECTION_SOURCE_DISABLED);
	rs->environment_set_tonemap(mask_environment, RenderingServer::ENV_TONE_MAPPER_LINEAR, 1.0, 1.0);

	for (int eye = 0; eye < 2; eye++) {
		mask_materials[eye].instantiate();
		mask_materials[eye]->set_shader(mask_shader);
		mask_materials[eye]->set_shader_parameter("mask_view_index", eye);
		mask_materials[eye]->set_shader_parameter("mask_filter_bias", mask_filter_bias * mask_filter_world_scale);
	}

	mask_scenario = rs->scenario_create();
	rs->scenario_set_environment(mask_scenario, mask_environment);

	mask_camera = rs->camera_create();
	rs->camera_set_perspective(mask_camera, 120.0, 0.01, 1000.0);
	rs->camera_set_cull_mask(mask_camera, 1);

	mask_viewport = rs->viewport_create();
	_update_mask_viewport_size();
	Viewport *parent_viewport = get_viewport();
	if (parent_viewport) {
		// Child viewports are rendered before their parent. Without this,
		// the XR viewport samples the mask before it is updated and the mask
		// is always one frame behind.
		rs->viewport_set_parent_viewport(mask_viewport, parent_viewport->get_viewport_rid());
	}
	rs->viewport_set_scenario(mask_viewport, mask_scenario);
	rs->viewport_attach_camera(mask_viewport, mask_camera);
	// The mask stores packed depth rather than display color. Keep the target
	// in the lower-bandwidth LDR path; the sampler decodes its sRGB storage.
	rs->viewport_set_use_hdr_2d(mask_viewport, false);
	rs->viewport_set_msaa_3d(mask_viewport, RenderingServer::VIEWPORT_MSAA_DISABLED);
	rs->viewport_set_use_taa(mask_viewport, false);
	rs->viewport_set_use_debanding(mask_viewport, false);
	rs->viewport_set_transparent_background(mask_viewport, false);
	rs->viewport_set_disable_2d(mask_viewport, true);
	rs->viewport_set_clear_mode(mask_viewport, RenderingServer::VIEWPORT_CLEAR_ALWAYS);
	rs->viewport_set_update_mode(mask_viewport, RenderingServer::VIEWPORT_UPDATE_DISABLED);
	mask_viewport_dirty = true;
	mask_update_accumulator = 0.0;

	for (MaskMeshEntry &entry : mask_meshes) {
		_create_mask_instances(entry);
	}

	_update_mask_filter_state();
}

void OpenXRMetaEnvironmentDepth::_free_mask_resources() {
	OpenXRMetaEnvironmentDepthExtension *env_depth_ext = OpenXRMetaEnvironmentDepthExtension::get_singleton();
	if (env_depth_ext) {
		env_depth_ext->set_reprojection_mask_filter_enabled(false);
		env_depth_ext->set_reprojection_mask_texture(RID());
	}

	RenderingServer *rs = RenderingServer::get_singleton();
	if (!rs) {
		return;
	}

	for (MaskMeshEntry &entry : mask_meshes) {
		_free_mask_instances(entry);
	}

	if (mask_viewport.is_valid()) {
		if (mask_viewport_active) {
			rs->viewport_set_active(mask_viewport, false);
		}
		rs->free_rid(mask_viewport);
		mask_viewport = RID();
	}
	if (mask_camera.is_valid()) {
		rs->free_rid(mask_camera);
		mask_camera = RID();
	}
	if (mask_scenario.is_valid()) {
		rs->free_rid(mask_scenario);
		mask_scenario = RID();
	}
	for (int eye = 0; eye < 2; eye++) {
		mask_materials[eye].unref();
	}
	mask_viewport_active = false;

	if (mask_environment.is_valid()) {
		rs->free_rid(mask_environment);
		mask_environment = RID();
	}
	mask_shader.unref();
	mask_viewport_size = Vector2i();
	mask_viewport_dirty = true;
	mask_update_accumulator = 0.0;
}

void OpenXRMetaEnvironmentDepth::_create_mask_instances(MaskMeshEntry &p_entry) {
	if (!mask_scenario.is_valid() || p_entry.instances[0].is_valid()) {
		return;
	}

	RenderingServer *rs = RenderingServer::get_singleton();
	ERR_FAIL_NULL(rs);
	for (int eye = 0; eye < 2; eye++) {
		p_entry.instances[eye] = rs->instance_create();
		rs->instance_set_scenario(p_entry.instances[eye], mask_scenario);
		rs->instance_set_layer_mask(p_entry.instances[eye], 1);
		rs->instance_set_ignore_culling(p_entry.instances[eye], true);
		rs->instance_geometry_set_cast_shadows_setting(p_entry.instances[eye], RenderingServer::SHADOW_CASTING_SETTING_OFF);
		rs->instance_geometry_set_material_override(p_entry.instances[eye], mask_materials[eye]->get_rid());
	}
}

void OpenXRMetaEnvironmentDepth::_free_mask_instances(MaskMeshEntry &p_entry) {
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!rs) {
		return;
	}
	for (int eye = 0; eye < 2; eye++) {
		if (p_entry.instances[eye].is_valid()) {
			rs->free_rid(p_entry.instances[eye]);
			p_entry.instances[eye] = RID();
		}
	}
	p_entry.mesh = RID();
	p_entry.transform_initialized = false;
}

void OpenXRMetaEnvironmentDepth::_sync_mask_meshes() {
	RenderingServer *rs = RenderingServer::get_singleton();
	ERR_FAIL_NULL(rs);

	_update_mask_viewport_size();
	XRServer *xr_server = XRServer::get_singleton();
	const float world_scale = xr_server ? static_cast<float>(xr_server->get_world_scale()) : 1.0f;
	if (!Math::is_equal_approx(mask_filter_world_scale, world_scale)) {
		mask_filter_world_scale = world_scale;
		for (int eye = 0; eye < 2; eye++) {
			if (mask_materials[eye].is_valid()) {
				mask_materials[eye]->set_shader_parameter("mask_filter_bias", mask_filter_bias * mask_filter_world_scale);
			}
		}
		mask_viewport_dirty = true;
	}

	for (uint32_t i = 0; i < mask_meshes.size();) {
		MaskMeshEntry &entry = mask_meshes[i];
		MeshInstance3D *source = Object::cast_to<MeshInstance3D>(ObjectDB::get_instance(entry.source_id));
		if (!source) {
			_free_mask_instances(entry);
			mask_meshes.remove_at(i);
			continue;
		}

		_create_mask_instances(entry);
		Ref<Mesh> mesh = source->get_mesh();
		RID mesh_rid = mesh.is_valid() ? mesh->get_rid() : RID();
		if (entry.mesh != mesh_rid) {
			entry.mesh = mesh_rid;
			for (int eye = 0; eye < 2; eye++) {
				rs->instance_set_base(entry.instances[eye], mesh_rid);
			}
			mask_viewport_dirty = true;
		}

		// The depth projection-view globals operate in Godot world space.
		Transform3D transform = source->get_global_transform();
		if (!entry.transform_initialized || entry.transform != transform) {
			entry.transform = transform;
			entry.transform_initialized = true;
			for (int eye = 0; eye < 2; eye++) {
				rs->instance_set_transform(entry.instances[eye], transform);
			}
			mask_viewport_dirty = true;
		}
		i++;
	}

	if (mask_meshes.is_empty()) {
		_free_mask_resources();
		return;
	}

	_update_mask_filter_state();
}

void OpenXRMetaEnvironmentDepth::_update_mask_viewport_size() {
	if (!mask_viewport.is_valid()) {
		return;
	}

	Vector2i depth_size = OpenXRMetaEnvironmentDepthExtension::get_singleton()->get_environment_depth_texture_size();
	if (depth_size.x <= 0 || depth_size.y <= 0) {
		depth_size = Vector2i(320, 320);
	}
	Vector2i scaled_size(
			MAX(1, static_cast<int>(Math::round(depth_size.x * mask_filter_resolution_scale))),
			MAX(1, static_cast<int>(Math::round(depth_size.y * mask_filter_resolution_scale))));
	if (scaled_size == mask_viewport_size) {
		return;
	}

	mask_viewport_size = scaled_size;
	RenderingServer *rs = RenderingServer::get_singleton();
	ERR_FAIL_NULL(rs);
	rs->viewport_set_size(mask_viewport, mask_viewport_size.x * 2, mask_viewport_size.y);
	mask_viewport_dirty = true;
}

void OpenXRMetaEnvironmentDepth::_update_mask_filter_state() {
	OpenXRMetaEnvironmentDepthExtension *env_depth_ext = OpenXRMetaEnvironmentDepthExtension::get_singleton();
	if (!env_depth_ext) {
		return;
	}

	bool has_renderable_mesh = false;
	for (const MaskMeshEntry &entry : mask_meshes) {
		if (entry.mesh.is_valid()) {
			has_renderable_mesh = true;
			break;
		}
	}
	Ref<OpenXRAPIExtension> openxr_api = env_depth_ext->get_openxr_api();
	bool environment_depth_running = openxr_api.is_valid() && openxr_api->is_running() && env_depth_ext->is_environment_depth_started();
	bool active = mask_filter_enabled && environment_depth_running && is_visible_in_tree() && has_renderable_mesh && mask_viewport.is_valid();
	if (active == mask_viewport_active) {
		return;
	}

	RenderingServer *rs = RenderingServer::get_singleton();
	ERR_FAIL_NULL(rs);
	ERR_FAIL_COND(!mask_viewport.is_valid());
	rs->viewport_set_active(mask_viewport, active);
	if (active) {
		env_depth_ext->set_reprojection_mask_texture(rs->viewport_get_texture(mask_viewport));
		mask_viewport_dirty = true;
		mask_update_accumulator = 0.0;
	} else {
		rs->viewport_set_update_mode(mask_viewport, RenderingServer::VIEWPORT_UPDATE_DISABLED);
	}
	env_depth_ext->set_reprojection_mask_filter_enabled(active);
	mask_viewport_active = active;
	if (active) {
		_request_mask_viewport_update(true);
	}
}

void OpenXRMetaEnvironmentDepth::_request_mask_viewport_update(bool p_force) {
	if (!mask_viewport_active || !mask_viewport.is_valid()) {
		return;
	}

	const double update_interval = 1.0 / MAX(static_cast<double>(mask_filter_update_rate), 1.0);
	if (!p_force && !mask_viewport_dirty && mask_update_accumulator < update_interval) {
		return;
	}

	RenderingServer *rs = RenderingServer::get_singleton();
	ERR_FAIL_NULL(rs);
	rs->viewport_set_update_mode(mask_viewport, RenderingServer::VIEWPORT_UPDATE_ONCE);
	OpenXRMetaEnvironmentDepthExtension::get_singleton()->mark_reprojection_mask_dirty();
	mask_viewport_dirty = false;
	mask_update_accumulator = 0.0;
}

void OpenXRMetaEnvironmentDepth::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE:
		case NOTIFICATION_VISIBILITY_CHANGED: {
			_update_visibility();
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			if (mask_filter_enabled && !mask_meshes.is_empty()) {
				mask_update_accumulator += get_process_delta_time();
				_ensure_mask_resources();
				_sync_mask_meshes();
				_request_mask_viewport_update();
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			_free_mask_resources();
			_update_visibility();
		} break;
	}
}

void OpenXRMetaEnvironmentDepth::_update_visibility() {
	bool is_visible = false;
	OpenXRMetaEnvironmentDepthExtension *env_depth_ext = OpenXRMetaEnvironmentDepthExtension::get_singleton();
	if (env_depth_ext) {
		Ref<OpenXRAPIExtension> openxr_api = env_depth_ext->get_openxr_api();
		is_visible = is_visible_in_tree() && openxr_api->is_running() && env_depth_ext->is_environment_depth_started();
	}

	if (is_visible) {
		set_base(env_depth_ext->get_reprojection_mesh());
	} else {
		set_base(RID());
	}
	if (env_depth_ext) {
		env_depth_ext->set_reprojection_active(is_visible);
	}
	_update_mask_filter_state();
}

void OpenXRMetaEnvironmentDepth::_on_openxr_session_begun() {
	_update_visibility();
}

void OpenXRMetaEnvironmentDepth::_on_openxr_session_stopping() {
	_update_visibility();
}

void OpenXRMetaEnvironmentDepth::_on_environment_depth_started() {
	_update_visibility();
}

void OpenXRMetaEnvironmentDepth::_on_environment_depth_stopped() {
	_update_visibility();
}

PackedStringArray OpenXRMetaEnvironmentDepth::_get_configuration_warnings() const {
	PackedStringArray warnings = Node::_get_configuration_warnings();

	if (is_visible() && is_inside_tree()) {
		XRCamera3D *camera = Object::cast_to<XRCamera3D>(get_parent());
		if (camera == nullptr) {
			warnings.push_back("OpenXRMetaEnvironmentDepth must be a child of an XRCamera3D node.");
		} else if ((camera->get_cull_mask() & get_layer_mask()) == 0) {
			warnings.push_back("The parent XRCamera3D cull_mask does not include any of this node's VisualInstance3D layers, so environment depth will not be rendered by the XR camera.");
		}
	}

	return warnings;
}

OpenXRMetaEnvironmentDepth::OpenXRMetaEnvironmentDepth() {
	set_process_internal(true);

	XRServer *xr_server = XRServer::get_singleton();
	if (xr_server) {
		Ref<XRInterface> openxr_interface = XRServer::get_singleton()->find_interface("OpenXR");
		if (openxr_interface.is_valid()) {
			openxr_interface->connect("session_begun", callable_mp(this, &OpenXRMetaEnvironmentDepth::_on_openxr_session_begun));
			openxr_interface->connect("session_stopping", callable_mp(this, &OpenXRMetaEnvironmentDepth::_on_openxr_session_stopping));
		}
	}

	OpenXRMetaEnvironmentDepthExtension *env_depth_ext = OpenXRMetaEnvironmentDepthExtension::get_singleton();
	if (env_depth_ext) {
		env_depth_ext->connect("openxr_meta_environment_depth_started", callable_mp(this, &OpenXRMetaEnvironmentDepth::_on_environment_depth_started));
		env_depth_ext->connect("openxr_meta_environment_depth_stopped", callable_mp(this, &OpenXRMetaEnvironmentDepth::_on_environment_depth_stopped));
	}

	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs) {
		rs->instance_geometry_set_cast_shadows_setting(get_instance(), RenderingServer::SHADOW_CASTING_SETTING_OFF);
	}
}

OpenXRMetaEnvironmentDepth::~OpenXRMetaEnvironmentDepth() {
	_free_mask_resources();

	XRServer *xr_server = XRServer::get_singleton();
	if (xr_server) {
		Ref<XRInterface> openxr_interface = XRServer::get_singleton()->find_interface("OpenXR");
		if (openxr_interface.is_valid()) {
			openxr_interface->disconnect("session_begun", callable_mp(this, &OpenXRMetaEnvironmentDepth::_on_openxr_session_begun));
			openxr_interface->disconnect("session_stopping", callable_mp(this, &OpenXRMetaEnvironmentDepth::_on_openxr_session_stopping));
		}
	}

	OpenXRMetaEnvironmentDepthExtension *env_depth_ext = OpenXRMetaEnvironmentDepthExtension::get_singleton();
	if (env_depth_ext) {
		env_depth_ext->set_reprojection_active(false);
		env_depth_ext->disconnect("openxr_meta_environment_depth_started", callable_mp(this, &OpenXRMetaEnvironmentDepth::_on_environment_depth_started));
		env_depth_ext->disconnect("openxr_meta_environment_depth_stopped", callable_mp(this, &OpenXRMetaEnvironmentDepth::_on_environment_depth_stopped));
	}
}
