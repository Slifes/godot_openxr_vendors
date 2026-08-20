/**************************************************************************/
/*  openxr_meta_environment_depth.h                                       */
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

#pragma once

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/visual_instance3d.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/templates/local_vector.hpp>

namespace godot {
class OpenXRMetaEnvironmentDepth : public VisualInstance3D {
	GDCLASS(OpenXRMetaEnvironmentDepth, VisualInstance3D);

	struct MaskMeshEntry {
		ObjectID source_id;
		RID instances[2];
		RID mesh;
		Transform3D transform;
		bool transform_initialized = false;
	};

	bool mask_filter_enabled = false;
	float mask_filter_bias = 0.01;
	float mask_filter_resolution_scale = 1.0;
	float mask_filter_update_rate = 30.0;
	float mask_filter_world_scale = 1.0;
	double mask_update_accumulator = 0.0;
	bool mask_viewport_dirty = true;
	bool mask_viewport_active = false;
	Vector2i mask_viewport_size;
	RID mask_viewport;
	RID mask_scenario;
	RID mask_camera;
	RID mask_environment;
	Ref<Shader> mask_shader;
	Ref<ShaderMaterial> mask_materials[2];
	LocalVector<MaskMeshEntry> mask_meshes;

	void _update_visibility();
	void _ensure_mask_resources();
	void _free_mask_resources();
	void _create_mask_instances(MaskMeshEntry &p_entry);
	void _free_mask_instances(MaskMeshEntry &p_entry);
	void _sync_mask_meshes();
	void _update_mask_viewport_size();
	void _update_mask_filter_state();
	void _request_mask_viewport_update(bool p_force = false);

	void _on_openxr_session_begun();
	void _on_openxr_session_stopping();
	void _on_environment_depth_started();
	void _on_environment_depth_stopped();

protected:
	void _notification(int p_what);

	static void _bind_methods();

public:
	PackedStringArray _get_configuration_warnings() const override;

	void set_render_priority(int p_render_priority);
	int get_render_priority() const;

	void set_bilinear_filtering(bool p_enabled);
	bool get_bilinear_filtering() const;

	void set_reprojection_offset_scale(float p_offset_exponent);
	float get_reprojection_offset_scale() const;

	void set_reprojection_offset_exponent(float p_offset_exponent);
	float get_reprojection_offset_exponent() const;

	void set_mask_filter_enabled(bool p_enabled);
	bool get_mask_filter_enabled() const;

	void set_mask_filter_bias(float p_bias);
	float get_mask_filter_bias() const;

	void set_mask_filter_resolution_scale(float p_scale);
	float get_mask_filter_resolution_scale() const;

	void set_mask_filter_update_rate(float p_rate);
	float get_mask_filter_update_rate() const;

	void add_mask_mesh(MeshInstance3D *p_mesh);
	void remove_mask_mesh(MeshInstance3D *p_mesh);
	void clear_mask_meshes();
	Array get_mask_meshes() const;
	RID get_mask_depth_texture(int p_view_index) const;

	OpenXRMetaEnvironmentDepth();
	~OpenXRMetaEnvironmentDepth();
};
}; // namespace godot
