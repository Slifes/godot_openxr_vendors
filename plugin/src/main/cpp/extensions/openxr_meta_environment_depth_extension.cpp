/**************************************************************************/
/*  openxr_meta_environment_depth_extension_wrapper.cpp                   */
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

#include "extensions/openxr_meta_environment_depth_extension.h"

#ifdef ANDROID_ENABLED
#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <jni.h>

#define XR_USE_GRAPHICS_API_VULKAN
#include <vulkan/vulkan.h>

#include <openxr/openxr_platform.h>
#endif // ANDROID_ENABLED

#include <openxr/internal/xr_linear.h>

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/open_xrapi_extension.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/rd_pipeline_specialization_constant.hpp>
#include <godot_cpp/classes/rd_sampler_state.hpp>
#include <godot_cpp/classes/rd_shader_source.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/classes/xr_interface.hpp>
#include <godot_cpp/classes/xr_server.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

static const char *META_ENVIRONMENT_DEPTH_AVAILABLE_NAME = "META_ENVIRONMENT_DEPTH_AVAILABLE";
static const char *META_ENVIRONMENT_DEPTH_TEXTURE_NAME = "META_ENVIRONMENT_DEPTH_TEXTURE";
static const char *META_ENVIRONMENT_DEPTH_TEXEL_SIZE_NAME = "META_ENVIRONMENT_DEPTH_TEXEL_SIZE";
static const char *META_ENVIRONMENT_DEPTH_Z_BUFFER_PARAMS_NAME = "META_ENVIRONMENT_DEPTH_Z_BUFFER_PARAMS";
static const char *META_ENVIRONMENT_DEPTH_PROJECTION_VIEW_LEFT_NAME = "META_ENVIRONMENT_DEPTH_PROJECTION_VIEW_LEFT";
static const char *META_ENVIRONMENT_DEPTH_PROJECTION_VIEW_RIGHT_NAME = "META_ENVIRONMENT_DEPTH_PROJECTION_VIEW_RIGHT";
static const char *META_ENVIRONMENT_DEPTH_INV_PROJECTION_VIEW_LEFT_NAME = "META_ENVIRONMENT_DEPTH_INV_PROJECTION_VIEW_LEFT";
static const char *META_ENVIRONMENT_DEPTH_INV_PROJECTION_VIEW_RIGHT_NAME = "META_ENVIRONMENT_DEPTH_INV_PROJECTION_VIEW_RIGHT";
static const char *META_ENVIRONMENT_DEPTH_FROM_CAMERA_PROJECTION_LEFT_NAME = "META_ENVIRONMENT_DEPTH_FROM_CAMERA_PROJECTION_LEFT";
static const char *META_ENVIRONMENT_DEPTH_FROM_CAMERA_PROJECTION_RIGHT_NAME = "META_ENVIRONMENT_DEPTH_FROM_CAMERA_PROJECTION_RIGHT";
static const char *META_ENVIRONMENT_DEPTH_TO_CAMERA_PROJECTION_LEFT_NAME = "META_ENVIRONMENT_DEPTH_TO_CAMERA_PROJECTION_LEFT";
static const char *META_ENVIRONMENT_DEPTH_TO_CAMERA_PROJECTION_RIGHT_NAME = "META_ENVIRONMENT_DEPTH_TO_CAMERA_PROJECTION_RIGHT";

static const char *META_ENVIRONMENT_DEPTH_REPROJECTION_SHADER_CODE = R"(
shader_type spatial;
render_mode unshaded, shadow_to_opacity, depth_draw_always, shadows_disabled, cull_disabled, fog_disabled;
global uniform bool META_ENVIRONMENT_DEPTH_AVAILABLE;
//DEFINES
#ifdef USE_PREPROJECTED_DEPTH
uniform highp sampler2DArray reprojected_depth_texture : filter_nearest, repeat_disable, hint_default_black;
#else
global uniform highp sampler2DArray META_ENVIRONMENT_DEPTH_TEXTURE : filter_nearest, repeat_disable, hint_default_black;
global uniform highp vec2 META_ENVIRONMENT_DEPTH_TEXEL_SIZE;
global uniform highp mat4 META_ENVIRONMENT_DEPTH_FROM_CAMERA_PROJECTION_LEFT;
global uniform highp mat4 META_ENVIRONMENT_DEPTH_FROM_CAMERA_PROJECTION_RIGHT;
global uniform highp mat4 META_ENVIRONMENT_DEPTH_TO_CAMERA_PROJECTION_LEFT;
global uniform highp mat4 META_ENVIRONMENT_DEPTH_TO_CAMERA_PROJECTION_RIGHT;
#ifdef USE_MASK_FILTER
uniform highp sampler2DArray filtered_depth_texture : filter_nearest, repeat_disable, hint_default_black;
uniform highp sampler2D mask_depth_texture : filter_nearest, repeat_disable, source_color, hint_default_black;
// 0: disabled, 1: GPU-prefiltered depth, 2: legacy per-fragment mask fallback.
uniform int mask_filter_mode = 0;
#endif // USE_MASK_FILTER
#endif // USE_PREPROJECTED_DEPTH
#ifndef USE_PREPROJECTED_DEPTH
#ifdef USE_DEPTH_OFFSET_SCALE
uniform highp float depth_offset_scale = 0.0;
#ifdef USE_DEPTH_OFFSET_EXPONENT
uniform highp float depth_offset_exponent = 1.0;
#endif // USE_DEPTH_OFFSET_EXPONENT
#endif // USE_DEPTH_OFFSET_SCALE
#endif // USE_PREPROJECTED_DEPTH
#ifndef USE_PREPROJECTED_DEPTH
varying highp vec4 depth_reprojected_clip;
#endif
void vertex() {
	UV = VERTEX.xy * 0.5 + 0.5;
	highp vec4 clip = vec4(VERTEX.xyz, 1.0);
#ifndef USE_PREPROJECTED_DEPTH
	highp mat4 camera_to_depth_proj = (VIEW_INDEX == VIEW_MONO_LEFT) ? META_ENVIRONMENT_DEPTH_FROM_CAMERA_PROJECTION_LEFT : META_ENVIRONMENT_DEPTH_FROM_CAMERA_PROJECTION_RIGHT;
	// This transform is linear in clip space. Interpolating its homogeneous
	// result is exact because every fullscreen-triangle vertex has w = 1.
	depth_reprojected_clip = camera_to_depth_proj * clip;
#endif
	POSITION = clip;
}
#ifndef USE_PREPROJECTED_DEPTH
#ifdef USE_BILINEAR_FILTERING
float get_depth_bilinear(vec2 uv, uint view_index) {
	vec2 p = uv / META_ENVIRONMENT_DEPTH_TEXEL_SIZE - vec2(0.5);
	vec2 f = fract(p);
	vec2 i = floor(p);

	vec2 uv00 = (i + vec2(0.5, 0.5)) * META_ENVIRONMENT_DEPTH_TEXEL_SIZE;
	vec2 uv10 = uv00 + vec2(META_ENVIRONMENT_DEPTH_TEXEL_SIZE.x, 0.0);
	vec2 uv01 = uv00 + vec2(0.0, META_ENVIRONMENT_DEPTH_TEXEL_SIZE.y);
	vec2 uv11 = uv00 + META_ENVIRONMENT_DEPTH_TEXEL_SIZE;

	float d00 = texture(META_ENVIRONMENT_DEPTH_TEXTURE, vec3(uv00, float(view_index))).r;
	float d10 = texture(META_ENVIRONMENT_DEPTH_TEXTURE, vec3(uv10, float(view_index))).r;
	float d01 = texture(META_ENVIRONMENT_DEPTH_TEXTURE, vec3(uv01, float(view_index))).r;
	float d11 = texture(META_ENVIRONMENT_DEPTH_TEXTURE, vec3(uv11, float(view_index))).r;

	return mix(mix(d00, d10, f.x), mix(d01, d11, f.x), f.y);
}
#endif // USE_BILINEAR_FILTERING
#ifdef USE_MASK_FILTER
float decode_mask_depth(vec3 encoded_depth) {
	return dot(encoded_depth, vec3(1.0, 1.0 / 255.0, 1.0 / 65025.0));
}
highp float get_mask_depth(vec2 uv, uint view_index) {
	highp vec2 atlas_uv = vec2(uv.x * 0.5 + float(view_index) * 0.5, uv.y);
	return decode_mask_depth(texture(mask_depth_texture, atlas_uv).rgb);
}
#endif // USE_MASK_FILTER
#endif // USE_PREPROJECTED_DEPTH
void fragment() {
	if (!META_ENVIRONMENT_DEPTH_AVAILABLE) {
		discard;
	}
#ifdef USE_PREPROJECTED_DEPTH
	highp float camera_depth = texture(reprojected_depth_texture, vec3(UV, float(VIEW_INDEX))).r;
	if (camera_depth == 0.0) {
		discard;
	}
	ALBEDO = vec3(0.0, 0.0, 0.0);
	DEPTH = camera_depth;
#else
	uint view_index = uint(VIEW_INDEX);
	highp mat4 depth_to_camera_proj = (VIEW_INDEX == VIEW_MONO_LEFT) ? META_ENVIRONMENT_DEPTH_TO_CAMERA_PROJECTION_LEFT : META_ENVIRONMENT_DEPTH_TO_CAMERA_PROJECTION_RIGHT;
	highp vec4 reprojected = depth_reprojected_clip;
	reprojected /= reprojected.w;
	highp vec2 reprojected_uv = reprojected.xy * 0.5 + 0.5;
	highp float depth = 0.0;
	bool inside_depth_texture = reprojected_uv.x >= 0.0 && reprojected_uv.y >= 0.0 && reprojected_uv.x <= 1.0 && reprojected_uv.y <= 1.0;
	if (inside_depth_texture) {
#ifdef USE_MASK_FILTER
		if (mask_filter_mode == 1) {
			// The low-resolution compute pass already combined Meta depth and
			// the mesh mask. This is the only texture read in the fullscreen
			// path when the optimized filter is active.
			depth = texture(filtered_depth_texture, vec3(reprojected_uv, float(view_index))).r;
		} else {
#ifdef USE_BILINEAR_FILTERING
			depth = get_depth_bilinear(reprojected_uv, view_index);
#else
			depth = texture(META_ENVIRONMENT_DEPTH_TEXTURE, vec3(reprojected_uv, float(view_index))).r;
#endif
		}
#else
#ifdef USE_BILINEAR_FILTERING
		depth = get_depth_bilinear(reprojected_uv, view_index);
#else
		depth = texture(META_ENVIRONMENT_DEPTH_TEXTURE, vec3(reprojected_uv, float(view_index))).r;
#endif
#endif // USE_MASK_FILTER
	}
	highp float mask_depth = 1.0;
#ifdef USE_MASK_FILTER
	if (mask_filter_mode == 2 && inside_depth_texture) {
		mask_depth = get_mask_depth(reprojected_uv, view_index);
	}
	bool is_masked = mask_filter_mode == 2 && depth != 0.0 && mask_depth < depth;
#else
	bool is_masked = false;
#endif

	if (depth == 0.0 || is_masked) {
		discard;
	}
	highp vec4 clip_back = vec4(reprojected.xy, depth * 2.0 - 1.0, 1.0);
	clip_back = depth_to_camera_proj * clip_back;

#ifdef USE_DEPTH_OFFSET_SCALE
#ifdef USE_DEPTH_OFFSET_EXPONENT
	highp float z_adjustment = pow(abs(clip_back.w), depth_offset_exponent) * depth_offset_scale;
#else
	highp float z_adjustment = abs(clip_back.w) * depth_offset_scale;
#endif // USE_DEPTH_OFFSET_EXPONENT
#if CURRENT_RENDERER != RENDERER_COMPATIBILITY
	z_adjustment *= -1.0;
#endif
	clip_back.z = clamp(clip_back.z + z_adjustment, -clip_back.w, clip_back.w);
#endif // USE_DEPTH_OFFSET_SCALE

	highp float camera_ndc_z = clip_back.z / clip_back.w;
#if CURRENT_RENDERER == RENDERER_COMPATIBILITY
	highp float camera_depth = 1.0 - (camera_ndc_z * 0.5 + 0.5);
#else
	highp float camera_depth = camera_ndc_z;
#endif
	ALBEDO = vec3(0.0, 0.0, 0.0);
	DEPTH = camera_depth;
#endif // USE_PREPROJECTED_DEPTH
}
)";

static const char *META_ENVIRONMENT_DEPTH_MASK_PREFILTER_SHADER_CODE = R"(
#version 450

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2DArray meta_depth_texture;
layout(set = 0, binding = 1) uniform sampler2D mask_depth_texture;
layout(OUTPUT_IMAGE_FORMAT, set = 0, binding = 2) uniform restrict writeonly image2DArray filtered_depth_texture;

float decode_mask_depth(vec3 encoded_depth) {
	return dot(encoded_depth, vec3(1.0, 1.0 / 255.0, 1.0 / 65025.0));
}

void main() {
	ivec3 pixel = ivec3(gl_GlobalInvocationID);
	ivec3 output_size = imageSize(filtered_depth_texture);
	if (any(greaterThanEqual(pixel, output_size))) {
		return;
	}

	float meta_depth = texelFetch(meta_depth_texture, pixel, 0).r;
	vec2 depth_uv = (vec2(pixel.xy) + vec2(0.5)) / vec2(output_size.xy);
	vec2 mask_atlas_uv = vec2(
			depth_uv.x * 0.5 + float(pixel.z) * 0.5,
			depth_uv.y);
	float mask_depth = decode_mask_depth(texture(mask_depth_texture, mask_atlas_uv).rgb);

	float filtered_depth = meta_depth;
	if (meta_depth != 0.0 && mask_depth < meta_depth) {
		filtered_depth = 0.0;
	}
	imageStore(filtered_depth_texture, pixel, vec4(filtered_depth));
}
)";

static const char *META_ENVIRONMENT_DEPTH_LOW_RES_REPROJECTION_SHADER_CODE = R"(
#version 450
//DEFINES

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2DArray source_depth_texture;
layout(OUTPUT_IMAGE_FORMAT, set = 0, binding = 1) uniform restrict writeonly image2DArray reprojected_depth_texture;
layout(set = 0, binding = 2, std140) uniform ReprojectionParameters {
	mat4 camera_to_depth[2];
	mat4 depth_to_camera[2];
	vec4 depth_offset;
} params;

#ifdef USE_BILINEAR_FILTERING
float get_depth_bilinear(vec2 uv, int view_index) {
	vec2 texture_size = vec2(textureSize(source_depth_texture, 0).xy);
	vec2 p = uv * texture_size - vec2(0.5);
	vec2 f = fract(p);
	ivec2 i = ivec2(floor(p));
	ivec2 max_pixel = ivec2(texture_size) - ivec2(1);

	float d00 = texelFetch(source_depth_texture, ivec3(clamp(i, ivec2(0), max_pixel), view_index), 0).r;
	float d10 = texelFetch(source_depth_texture, ivec3(clamp(i + ivec2(1, 0), ivec2(0), max_pixel), view_index), 0).r;
	float d01 = texelFetch(source_depth_texture, ivec3(clamp(i + ivec2(0, 1), ivec2(0), max_pixel), view_index), 0).r;
	float d11 = texelFetch(source_depth_texture, ivec3(clamp(i + ivec2(1, 1), ivec2(0), max_pixel), view_index), 0).r;
	return mix(mix(d00, d10, f.x), mix(d01, d11, f.x), f.y);
}
#endif

void main() {
	ivec3 pixel = ivec3(gl_GlobalInvocationID);
	ivec3 output_size = imageSize(reprojected_depth_texture);
	if (any(greaterThanEqual(pixel, output_size))) {
		return;
	}

	int view_index = pixel.z;
	vec2 camera_uv = (vec2(pixel.xy) + vec2(0.5)) / vec2(output_size.xy);
	vec4 depth_clip = params.camera_to_depth[view_index] * vec4(camera_uv * 2.0 - 1.0, 1.0, 1.0);
	depth_clip /= depth_clip.w;
	vec2 depth_uv = depth_clip.xy * 0.5 + 0.5;

	float camera_depth = 0.0;
	if (all(greaterThanEqual(depth_uv, vec2(0.0))) && all(lessThanEqual(depth_uv, vec2(1.0)))) {
#ifdef USE_BILINEAR_FILTERING
		float depth = get_depth_bilinear(depth_uv, view_index);
#else
		float depth = texture(source_depth_texture, vec3(depth_uv, float(view_index))).r;
#endif
		if (depth != 0.0) {
			vec4 camera_clip = params.depth_to_camera[view_index] * vec4(depth_clip.xy, depth * 2.0 - 1.0, 1.0);
			if (params.depth_offset.x != 0.0) {
				float z_adjustment = abs(camera_clip.w) * params.depth_offset.x;
				if (params.depth_offset.z != 0.0) {
					z_adjustment = pow(abs(camera_clip.w), params.depth_offset.y) * params.depth_offset.x;
				}
				camera_clip.z = clamp(camera_clip.z - z_adjustment, -camera_clip.w, camera_clip.w);
			}
			camera_depth = camera_clip.z / camera_clip.w;
		}
	}

	imageStore(reprojected_depth_texture, pixel, vec4(camera_depth));
}
)";

OpenXRMetaEnvironmentDepthExtension *OpenXRMetaEnvironmentDepthExtension::singleton = nullptr;

OpenXRMetaEnvironmentDepthExtension *OpenXRMetaEnvironmentDepthExtension::get_singleton() {
	if (singleton == nullptr) {
		singleton = memnew(OpenXRMetaEnvironmentDepthExtension());
	}
	return singleton;
}

OpenXRMetaEnvironmentDepthExtension::OpenXRMetaEnvironmentDepthExtension() :
		OpenXRExtensionWrapper() {
	ERR_FAIL_COND_MSG(singleton != nullptr, "An OpenXRMetaEnvironmentDepthExtension singleton already exists.");

	request_extensions[XR_META_ENVIRONMENT_DEPTH_EXTENSION_NAME] = &meta_environment_depth_ext;
	singleton = this;

#ifndef ANDROID_ENABLED
	render_state.graphics_api = GRAPHICS_API_UNSUPPORTED;
#endif // ANDROID_ENABLED
}

OpenXRMetaEnvironmentDepthExtension::~OpenXRMetaEnvironmentDepthExtension() {
	cleanup();
	singleton = nullptr;
}

void OpenXRMetaEnvironmentDepthExtension::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_environment_depth_supported"), &OpenXRMetaEnvironmentDepthExtension::is_environment_depth_supported);
	ClassDB::bind_method(D_METHOD("is_hand_removal_supported"), &OpenXRMetaEnvironmentDepthExtension::is_hand_removal_supported);

	ClassDB::bind_method(D_METHOD("start_environment_depth"), &OpenXRMetaEnvironmentDepthExtension::start_environment_depth);
	ClassDB::bind_method(D_METHOD("stop_environment_depth"), &OpenXRMetaEnvironmentDepthExtension::stop_environment_depth);
	ClassDB::bind_method(D_METHOD("is_environment_depth_started"), &OpenXRMetaEnvironmentDepthExtension::is_environment_depth_started);

	ClassDB::bind_method(D_METHOD("set_hand_removal_enabled", "enable"), &OpenXRMetaEnvironmentDepthExtension::set_hand_removal_enabled);
	ClassDB::bind_method(D_METHOD("get_hand_removal_enabled"), &OpenXRMetaEnvironmentDepthExtension::get_hand_removal_enabled);

	ClassDB::bind_method(D_METHOD("get_environment_depth_map_async", "callback"), &OpenXRMetaEnvironmentDepthExtension::get_environment_depth_map_async);

	ADD_SIGNAL(MethodInfo("openxr_meta_environment_depth_started"));
	ADD_SIGNAL(MethodInfo("openxr_meta_environment_depth_stopped"));
}

Dictionary OpenXRMetaEnvironmentDepthExtension::_get_requested_extensions(uint64_t p_xr_version) {
	Dictionary result;
	for (auto ext : request_extensions) {
		uint64_t value = reinterpret_cast<uint64_t>(ext.value);
		result[ext.key] = (Variant)value;
	}
	return result;
}

void OpenXRMetaEnvironmentDepthExtension::_on_instance_created(uint64_t instance) {
	if (meta_environment_depth_ext) {
		bool result = initialize_meta_environment_depth_extension((XrInstance)instance);
		if (!result) {
			UtilityFunctions::print("Failed to initialize XR_META_environment_depth extension");
			meta_environment_depth_ext = false;
		}
	}
}

void OpenXRMetaEnvironmentDepthExtension::_on_instance_destroyed() {
	cleanup();
}

void OpenXRMetaEnvironmentDepthExtension::_on_session_destroyed() {
	// Normally, we'd only run this on the render thread, but the session is being destroyed,
	// and we need to make sure to clean-up before that is done. At this point, we shouldn't
	// be rendering anything through OpenXR anymore anyway.
	_destroy_depth_provider_rt();
}

void OpenXRMetaEnvironmentDepthExtension::_set_depth_globals_unavailable_rt() {
	if (!render_state.globals_depth_available &&
			render_state.globals_depth_swapchain_index < 0 &&
			render_state.globals_depth_texel_size == Vector2()) {
		return;
	}

	RenderingServer *rs = RenderingServer::get_singleton();
	ERR_FAIL_NULL(rs);
	rs->global_shader_parameter_set(META_ENVIRONMENT_DEPTH_AVAILABLE_NAME, false);
	rs->global_shader_parameter_set(META_ENVIRONMENT_DEPTH_TEXTURE_NAME, RID());
	rs->global_shader_parameter_set(META_ENVIRONMENT_DEPTH_TEXEL_SIZE_NAME, Vector2());
	render_state.globals_depth_available = false;
	render_state.globals_depth_swapchain_index = -1;
	render_state.globals_depth_texel_size = Vector2();
	render_state.globals_depth_z_buffer_params_valid = false;
}

void OpenXRMetaEnvironmentDepthExtension::_on_pre_render() {
#ifdef ANDROID_ENABLED
	RenderingServer *rs = RenderingServer::get_singleton();
	ERR_FAIL_NULL(rs);

	if (unlikely(reprojection_material_dirty)) {
		update_reprojection_material();
	}

	render_state.current_depth_swapchain_index = -1;
	render_state.depth_reprojection_pending = false;

	if (render_state.depth_provider == XR_NULL_HANDLE || !render_state.depth_provider_started) {
		_set_depth_globals_unavailable_rt();
		return;
	}

	Ref<OpenXRAPIExtension> openxr_api = get_openxr_api();
	ERR_FAIL_COND(openxr_api.is_null());

	XRServer *xr_server = XRServer::get_singleton();
	ERR_FAIL_NULL(xr_server);

	Ref<XRInterface> openxr_interface = xr_server->find_interface("OpenXR");
	ERR_FAIL_COND(openxr_interface.is_null());

	XrEnvironmentDepthImageAcquireInfoMETA acquire_info = {
		XR_TYPE_ENVIRONMENT_DEPTH_IMAGE_ACQUIRE_INFO_META, // type
		nullptr, // next
		(XrSpace)openxr_api->get_play_space(),
		openxr_api->get_predicted_display_time(),
	};

	XrEnvironmentDepthImageMETA depth_image = {
		XR_TYPE_ENVIRONMENT_DEPTH_IMAGE_META, // type
		nullptr, // next
		0, // swapchainIndex
		0.0, // nearZ
		0.0, // farZ
		{
				// views
				{
						XR_TYPE_ENVIRONMENT_DEPTH_IMAGE_VIEW_META, // type
						nullptr, // next
				},
				{
						XR_TYPE_ENVIRONMENT_DEPTH_IMAGE_VIEW_META, // type
						nullptr, // next
				},
		}
	};

	XrResult result = xrAcquireEnvironmentDepthImageMETA(render_state.depth_provider, &acquire_info, &depth_image);
	if (result == XR_ENVIRONMENT_DEPTH_NOT_AVAILABLE_META) {
		// This is a success code, but the specification requires all output
		// fields to remain unchanged. Do not treat the zero-initialized image
		// as swapchain image 0.
		_set_depth_globals_unavailable_rt();
		return;
	}
	if (XR_FAILED(result)) {
		UtilityFunctions::printerr("Failed to acquire environment depth image: ", openxr_api->get_error_string(result));
		_set_depth_globals_unavailable_rt();
		return;
	}
	if (depth_image.swapchainIndex >= render_state.depth_swapchain_textures.size()) {
		UtilityFunctions::printerr(
				"Environment depth runtime returned invalid swapchain index ",
				depth_image.swapchainIndex,
				" for ",
				render_state.depth_swapchain_textures.size(),
				" imported images");
		_set_depth_globals_unavailable_rt();
		return;
	}
	render_state.current_depth_swapchain_index = static_cast<int32_t>(depth_image.swapchainIndex);

	if (render_state.globals_depth_swapchain_index != static_cast<int32_t>(depth_image.swapchainIndex)) {
		rs->global_shader_parameter_set(META_ENVIRONMENT_DEPTH_TEXTURE_NAME, render_state.depth_swapchain_textures[depth_image.swapchainIndex]);
		render_state.globals_depth_swapchain_index = static_cast<int32_t>(depth_image.swapchainIndex);
	}
	if (render_state.globals_depth_texel_size != render_state.depth_swapchain_texel_size) {
		rs->global_shader_parameter_set(META_ENVIRONMENT_DEPTH_TEXEL_SIZE_NAME, render_state.depth_swapchain_texel_size);
		render_state.globals_depth_texel_size = render_state.depth_swapchain_texel_size;
	}
	if (!render_state.globals_depth_available) {
		rs->global_shader_parameter_set(META_ENVIRONMENT_DEPTH_AVAILABLE_NAME, true);
		render_state.globals_depth_available = true;
	}

	const float world_scale = static_cast<float>(xr_server->get_world_scale());
	const float depth_near = depth_image.nearZ * world_scale;
	const float depth_far = depth_image.farZ * world_scale;
	Vector2 depth_z_buffer_params;
	if (std::isfinite(depth_far) && depth_far > depth_near) {
		depth_z_buffer_params.x = -2.0f * depth_near * depth_far / (depth_far - depth_near);
		depth_z_buffer_params.y = -(depth_far + depth_near) / (depth_far - depth_near);
	} else {
		depth_z_buffer_params.x = -2.0f * depth_near;
		depth_z_buffer_params.y = -1.0f;
	}
	if (!render_state.globals_depth_z_buffer_params_valid ||
			render_state.globals_depth_z_buffer_params != depth_z_buffer_params) {
		rs->global_shader_parameter_set(META_ENVIRONMENT_DEPTH_Z_BUFFER_PARAMS_NAME, depth_z_buffer_params);
		render_state.globals_depth_z_buffer_params = depth_z_buffer_params;
		render_state.globals_depth_z_buffer_params_valid = true;
	}

	const Transform3D world_origin = xr_server->get_world_origin();
	const Transform3D reference_frame = xr_server->get_reference_frame();
	Vector2 viewport_size = openxr_interface->get_render_target_size();
	float aspect = viewport_size.width / viewport_size.height;

	float z_near = openxr_api->get_render_state_z_near();
	float z_far = openxr_api->get_render_state_z_far();

	Array callback_data;

	for (int i = 0; i < 2; i++) {
		Transform3D depth_eye_transform = OpenXRUtilities::xrPosef_to_godot_transform3d(depth_image.views[i].pose);
		depth_eye_transform.origin *= world_scale;
		const Transform3D depth_eye_world_transform = world_origin * reference_frame * depth_eye_transform;

		XrMatrix4x4f projection_mat;
		XrMatrix4x4f_CreateProjectionFov(
				&projection_mat,
				GRAPHICS_OPENGL,
				depth_image.views[i].fov,
				depth_near,
				std::isfinite(depth_far) ? depth_far : 0);

		Projection godot_projection_mat;
		OpenXRUtilities::xrMatrix4x4f_to_godot_projection(&projection_mat, godot_projection_mat);

		// The returned depth pose is expressed in acquire_info.space. Match the
		// transform path used by OpenXRInterface::get_transform_for_view() so
		// these matrices operate on Godot world-space positions.
		Projection depth_proj_view = godot_projection_mat * depth_eye_world_transform.affine_inverse();
		Projection depth_inv_proj_view = depth_proj_view.inverse();
		rs->global_shader_parameter_set(i == 0 ? META_ENVIRONMENT_DEPTH_PROJECTION_VIEW_LEFT_NAME : META_ENVIRONMENT_DEPTH_PROJECTION_VIEW_RIGHT_NAME, depth_proj_view);
		rs->global_shader_parameter_set(i == 0 ? META_ENVIRONMENT_DEPTH_INV_PROJECTION_VIEW_LEFT_NAME : META_ENVIRONMENT_DEPTH_INV_PROJECTION_VIEW_RIGHT_NAME, depth_inv_proj_view);

		Projection camera_proj_view = openxr_interface->get_projection_for_view(i, aspect, z_near, z_far) * openxr_interface->get_transform_for_view(i, world_origin).affine_inverse();

		if (render_state.graphics_api == GRAPHICS_API_VULKAN) {
			Projection correction;
			correction.set_depth_correction(true);
			camera_proj_view = correction * camera_proj_view;
		}

		render_state.camera_to_depth[i] = depth_proj_view * camera_proj_view.inverse();
		render_state.depth_to_camera[i] = camera_proj_view * depth_inv_proj_view;
		rs->global_shader_parameter_set(i == 0 ? META_ENVIRONMENT_DEPTH_FROM_CAMERA_PROJECTION_LEFT_NAME : META_ENVIRONMENT_DEPTH_FROM_CAMERA_PROJECTION_RIGHT_NAME, render_state.camera_to_depth[i]);
		rs->global_shader_parameter_set(i == 0 ? META_ENVIRONMENT_DEPTH_TO_CAMERA_PROJECTION_LEFT_NAME : META_ENVIRONMENT_DEPTH_TO_CAMERA_PROJECTION_RIGHT_NAME, render_state.depth_to_camera[i]);

		if (render_state.depth_map_callbacks.size() > 0) {
			Dictionary data;
			data["depth_projection_view"] = depth_proj_view;
			data["depth_inverse_projection_view"] = depth_inv_proj_view;

			Ref<Image> image = rs->texture_2d_layer_get(render_state.depth_swapchain_textures[depth_image.swapchainIndex], i);
			data["image"] = image;

			callback_data.push_back(data);
		}
	}
	render_state.depth_reprojection_pending = true;

	if (render_state.depth_map_callbacks.size() > 0) {
		for (const Variant &v : render_state.depth_map_callbacks) {
			Callable callback = v;
			if (callback.is_valid()) {
				callback.call_deferred(callback_data);
			}
		}
		render_state.depth_map_callbacks.clear();
	}
#endif // ANDROID_ENABLED
}

void OpenXRMetaEnvironmentDepthExtension::_on_pre_draw_viewport(const RID &p_viewport) {
	(void)p_viewport;
#ifdef ANDROID_ENABLED
	if (!render_state.depth_reprojection_pending) {
		return;
	}
	render_state.depth_reprojection_pending = false;

	if (!reprojection_active ||
			render_state.graphics_api != GRAPHICS_API_VULKAN ||
			render_state.current_depth_swapchain_index < 0 ||
			reprojection_material.is_null()) {
		return;
	}

	const uint32_t swapchain_index = static_cast<uint32_t>(render_state.current_depth_swapchain_index);
	bool use_filtered_depth = false;
	if (reprojection_mask_filter_enabled && reprojection_mask_texture.is_valid()) {
		// The acquired swapchain image may be updated without changing its
		// index, so refresh the small mask pass on every acquired frame.
		use_filtered_depth = _dispatch_mask_prefilter_rt(swapchain_index);
		reprojection_mask_dirty.store(false);
	} else {
		if (mask_prefilter.filtered_depth_texture.is_valid()) {
			_free_mask_prefilter_resources_rt();
		}
	}

	_dispatch_depth_reprojection_rt(swapchain_index, use_filtered_depth);
#endif // ANDROID_ENABLED
}

uint64_t OpenXRMetaEnvironmentDepthExtension::_set_system_properties_and_get_next_pointer(void *p_next_pointer) {
	if (meta_environment_depth_ext) {
		system_depth_properties.next = p_next_pointer;
		return reinterpret_cast<uint64_t>(&system_depth_properties);
	}

	return reinterpret_cast<uint64_t>(p_next_pointer);
}

void OpenXRMetaEnvironmentDepthExtension::start_environment_depth() {
	RenderingServer *rs = RenderingServer::get_singleton();
	ERR_FAIL_NULL(rs);
	ERR_FAIL_COND(depth_provider_started);
	ERR_FAIL_COND_MSG(!is_environment_depth_supported(), "Meta environment depth is not supported by this system or graphics API.");

	depth_provider_started = true;
	setup_global_uniforms();

	rs->call_on_render_thread(callable_mp(this, &OpenXRMetaEnvironmentDepthExtension::_start_environment_depth_rt));

	emit_signal("openxr_meta_environment_depth_started");
}

void OpenXRMetaEnvironmentDepthExtension::stop_environment_depth() {
	RenderingServer *rs = RenderingServer::get_singleton();
	ERR_FAIL_NULL(rs);
	ERR_FAIL_COND(!depth_provider_started);

	depth_provider_started = false;

	rs->call_on_render_thread(callable_mp(this, &OpenXRMetaEnvironmentDepthExtension::_stop_environment_depth_rt));

	emit_signal("openxr_meta_environment_depth_stopped");
}

bool OpenXRMetaEnvironmentDepthExtension::is_environment_depth_started() {
	return depth_provider_started;
}

void OpenXRMetaEnvironmentDepthExtension::set_hand_removal_enabled(bool p_enable) {
	RenderingServer *rs = RenderingServer::get_singleton();
	ERR_FAIL_NULL(rs);
	if (!is_hand_removal_supported()) {
		hand_removal_enabled = false;
		ERR_FAIL_COND_MSG(p_enable, "Meta environment depth hand removal is not supported by this system.");
		return;
	}
	hand_removal_enabled = p_enable;
	rs->call_on_render_thread(callable_mp(this, &OpenXRMetaEnvironmentDepthExtension::_set_hand_removal_enabled_rt).bind(p_enable));
}

bool OpenXRMetaEnvironmentDepthExtension::get_hand_removal_enabled() const {
	return hand_removal_enabled;
}

RID OpenXRMetaEnvironmentDepthExtension::get_reprojection_mesh() {
	if (reprojection_mesh.is_null()) {
		reprojection_shader.instantiate();
		reprojection_material.instantiate();

		update_reprojection_material(true);

		reprojection_material->set_render_priority(reprojection_render_priority);

		PackedVector3Array vertices;
		vertices.resize(3);
		vertices[0] = Vector3(-1.0f, -1.0f, 1.0f);
		vertices[1] = Vector3(3.0f, -1.0f, 1.0f);
		vertices[2] = Vector3(-1.0f, 3.0f, 1.0f);

		Array arr;
		arr.resize(RenderingServer::ARRAY_MAX);
		arr[RenderingServer::ARRAY_VERTEX] = vertices;

		reprojection_mesh.instantiate();
		reprojection_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arr);
		reprojection_mesh->surface_set_material(0, reprojection_material);
		reprojection_mesh->set_custom_aabb(AABB(Vector3(-1000, -1000, -1000), Vector3(2000, 2000, 2000)));
	}

	return reprojection_mesh->get_rid();
}

void OpenXRMetaEnvironmentDepthExtension::update_reprojection_material(bool p_creation) {
	if (reprojection_shader.is_null() || reprojection_material.is_null()) {
		return;
	}

	String shader_code = META_ENVIRONMENT_DEPTH_REPROJECTION_SHADER_CODE;
	PackedStringArray defines;

	const bool use_preprojected_depth = depth_reprojection.reprojected_depth_texture.is_valid();
	if (use_preprojected_depth) {
		defines.append("#define USE_PREPROJECTED_DEPTH");
	} else {
		if (reprojection_offset_scale != 0.0) {
			defines.append("#define USE_DEPTH_OFFSET_SCALE");
		}
		if (reprojection_offset_exponent != 1.0) {
			defines.append("#define USE_DEPTH_OFFSET_EXPONENT");
		}
		if (reprojection_bilinear_filtering) {
			defines.append("#define USE_BILINEAR_FILTERING");
		}
		if (reprojection_mask_filter_enabled) {
			defines.append("#define USE_MASK_FILTER");
		}
	}

	shader_code = shader_code.replace("//DEFINES", String("\n").join(defines));

	reprojection_shader->set_code(shader_code);

	if (p_creation) {
		reprojection_material->set_shader(reprojection_shader);
	}

	if (use_preprojected_depth) {
		reprojection_material->set_shader_parameter("reprojected_depth_texture", depth_reprojection.reprojected_depth_texture);
	} else {
		if (reprojection_offset_scale != 0.0) {
			reprojection_material->set_shader_parameter("depth_offset_scale", reprojection_offset_scale);
		}
		if (reprojection_offset_exponent != 1.0) {
			reprojection_material->set_shader_parameter("depth_offset_exponent", reprojection_offset_exponent);
		}
		if (reprojection_mask_filter_enabled) {
			const bool prefiltered = mask_prefilter.filtered_depth_texture.is_valid();
			reprojection_material->set_shader_parameter("mask_filter_mode", prefiltered ? 1 : 2);
			reprojection_material->set_shader_parameter("filtered_depth_texture", mask_prefilter.filtered_depth_texture);
			reprojection_material->set_shader_parameter("mask_depth_texture", reprojection_mask_texture);
		}
	}

	reprojection_material_dirty = false;
}

void OpenXRMetaEnvironmentDepthExtension::set_reprojection_render_priority(int p_render_priority) {
	reprojection_render_priority = p_render_priority;

	if (reprojection_material.is_valid()) {
		reprojection_material->set_render_priority(p_render_priority);
	}
}

int OpenXRMetaEnvironmentDepthExtension::get_reprojection_render_priority() const {
	return reprojection_render_priority;
}

void OpenXRMetaEnvironmentDepthExtension::set_reprojection_offset_scale(float p_offset_scale) {
	reprojection_offset_scale = p_offset_scale;
	reprojection_material_dirty = true;
}

float OpenXRMetaEnvironmentDepthExtension::get_reprojection_offset_scale() const {
	return reprojection_offset_scale;
}

void OpenXRMetaEnvironmentDepthExtension::set_reprojection_offset_exponent(float p_offset_exponent) {
	reprojection_offset_exponent = p_offset_exponent;
	reprojection_material_dirty = true;
}

float OpenXRMetaEnvironmentDepthExtension::get_reprojection_offset_exponent() const {
	return reprojection_offset_exponent;
}

void OpenXRMetaEnvironmentDepthExtension::set_reprojection_bilinear_filtering(bool p_enabled) {
	if (reprojection_bilinear_filtering == p_enabled) {
		return;
	}
	reprojection_bilinear_filtering = p_enabled;
	reprojection_material_dirty = true;
	depth_reprojection.initialization_failed = false;
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs && depth_reprojection.reprojected_depth_texture.is_valid()) {
		rs->call_on_render_thread(callable_mp(this, &OpenXRMetaEnvironmentDepthExtension::_free_depth_reprojection_resources_rt));
	}
}

bool OpenXRMetaEnvironmentDepthExtension::get_reprojection_bilinear_filtering() const {
	return reprojection_bilinear_filtering;
}

void OpenXRMetaEnvironmentDepthExtension::set_reprojection_mask_filter_enabled(bool p_enabled) {
	if (reprojection_mask_filter_enabled == p_enabled) {
		return;
	}
	reprojection_mask_filter_enabled = p_enabled;
	reprojection_mask_dirty.store(true);
	reprojection_material_dirty = true;
	if (reprojection_material.is_valid()) {
		const bool prefiltered = p_enabled && mask_prefilter.filtered_depth_texture.is_valid();
		reprojection_material->set_shader_parameter("mask_filter_mode", prefiltered ? 1 : (p_enabled ? 2 : 0));
	}
}

void OpenXRMetaEnvironmentDepthExtension::set_reprojection_mask_texture(const RID &p_texture) {
	if (reprojection_mask_texture != p_texture) {
		reprojection_mask_dirty.store(true);
	}
	reprojection_mask_texture = p_texture;
	if (reprojection_material.is_valid()) {
		reprojection_material->set_shader_parameter("mask_depth_texture", p_texture);
	}
}

void OpenXRMetaEnvironmentDepthExtension::mark_reprojection_mask_dirty() {
	reprojection_mask_dirty.store(true);
}

void OpenXRMetaEnvironmentDepthExtension::set_reprojection_active(bool p_active) {
	reprojection_active = p_active;
}

bool OpenXRMetaEnvironmentDepthExtension::_ensure_mask_prefilter_resources_rt() {
#ifdef ANDROID_ENABLED
	if (mask_prefilter.initialization_failed) {
		return false;
	}
	if (render_state.graphics_api != GRAPHICS_API_VULKAN ||
			!reprojection_mask_texture.is_valid() ||
			render_state.depth_swapchain_textures.is_empty()) {
		return false;
	}

	RenderingServer *rs = RenderingServer::get_singleton();
	ERR_FAIL_NULL_V(rs, false);
	RenderingDevice *rd = rs->get_rendering_device();
	ERR_FAIL_NULL_V(rd, false);

	const Vector2i depth_size(depth_swapchain_width.load(), depth_swapchain_height.load());
	if (depth_size.x <= 0 || depth_size.y <= 0) {
		return false;
	}

	const RID mask_rd_texture = rs->texture_get_rd_texture(reprojection_mask_texture, true);
	if (!mask_rd_texture.is_valid()) {
		return false;
	}

	if (mask_prefilter.filtered_depth_texture.is_valid() &&
			(mask_prefilter.texture_size != depth_size ||
					mask_prefilter.mask_rd_texture != mask_rd_texture ||
					mask_prefilter.uniform_sets.size() != render_state.depth_swapchain_textures.size())) {
		_free_mask_prefilter_resources_rt();
	}
	if (mask_prefilter.filtered_depth_texture.is_valid()) {
		return true;
	}

	const BitField<RenderingDevice::TextureUsageBits> texture_usage =
			RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
			RenderingDevice::TEXTURE_USAGE_STORAGE_BIT;
	// Preserve the source D16 normalized depth precision. Half-float would use
	// the same bandwidth, but its coarse mantissa near 1.0 creates large
	// world-space depth steps after perspective unprojection.
	RenderingDevice::DataFormat output_format = RenderingDevice::DATA_FORMAT_R16_UNORM;
	String output_image_format = "r16";
	if (!rd->texture_is_format_supported_for_usage(output_format, texture_usage)) {
		output_format = RenderingDevice::DATA_FORMAT_R32_SFLOAT;
		output_image_format = "r32f";
	}
	if (!rd->texture_is_format_supported_for_usage(output_format, texture_usage)) {
		UtilityFunctions::printerr("[DepthMask] No sampleable storage texture format is available for the GPU prefilter.");
		mask_prefilter.initialization_failed = true;
		return false;
	}

	Ref<RDSamplerState> sampler_state;
	sampler_state.instantiate();
	sampler_state->set_mag_filter(RenderingDevice::SAMPLER_FILTER_NEAREST);
	sampler_state->set_min_filter(RenderingDevice::SAMPLER_FILTER_NEAREST);
	sampler_state->set_mip_filter(RenderingDevice::SAMPLER_FILTER_NEAREST);
	sampler_state->set_repeat_u(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
	sampler_state->set_repeat_v(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
	sampler_state->set_repeat_w(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
	mask_prefilter.sampler = rd->sampler_create(sampler_state);
	if (!mask_prefilter.sampler.is_valid()) {
		UtilityFunctions::printerr("[DepthMask] Failed to create the GPU prefilter sampler.");
		mask_prefilter.initialization_failed = true;
		return false;
	}

	String compute_shader_code = META_ENVIRONMENT_DEPTH_MASK_PREFILTER_SHADER_CODE;
	compute_shader_code = compute_shader_code.replace("OUTPUT_IMAGE_FORMAT", output_image_format);
	Ref<RDShaderSource> shader_source;
	shader_source.instantiate();
	shader_source->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
	shader_source->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE, compute_shader_code);
	Ref<RDShaderSPIRV> shader_spirv = rd->shader_compile_spirv_from_source(shader_source);
	if (shader_spirv.is_null()) {
		UtilityFunctions::printerr("[DepthMask] GPU prefilter shader compilation returned no SPIR-V.");
		_free_mask_prefilter_resources_rt();
		mask_prefilter.initialization_failed = true;
		return false;
	}
	const String compile_error = shader_spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
	if (!compile_error.is_empty()) {
		UtilityFunctions::printerr("[DepthMask] GPU prefilter shader compilation failed:\n", compile_error);
		_free_mask_prefilter_resources_rt();
		mask_prefilter.initialization_failed = true;
		return false;
	}

	mask_prefilter.shader = rd->shader_create_from_spirv(shader_spirv, "Meta environment depth mask prefilter");
	if (!mask_prefilter.shader.is_valid()) {
		UtilityFunctions::printerr("[DepthMask] Failed to create the GPU prefilter shader.");
		_free_mask_prefilter_resources_rt();
		mask_prefilter.initialization_failed = true;
		return false;
	}
	mask_prefilter.pipeline = rd->compute_pipeline_create(mask_prefilter.shader);
	if (!mask_prefilter.pipeline.is_valid()) {
		UtilityFunctions::printerr("[DepthMask] Failed to create the GPU prefilter pipeline.");
		_free_mask_prefilter_resources_rt();
		mask_prefilter.initialization_failed = true;
		return false;
	}

	Ref<RDTextureFormat> texture_format;
	texture_format.instantiate();
	texture_format->set_format(output_format);
	texture_format->set_width(depth_size.x);
	texture_format->set_height(depth_size.y);
	texture_format->set_depth(1);
	texture_format->set_array_layers(2);
	texture_format->set_mipmaps(1);
	texture_format->set_texture_type(RenderingDevice::TEXTURE_TYPE_2D_ARRAY);
	texture_format->set_samples(RenderingDevice::TEXTURE_SAMPLES_1);
	texture_format->set_usage_bits(texture_usage);

	Ref<RDTextureView> texture_view;
	texture_view.instantiate();
	mask_prefilter.filtered_depth_rd_texture = rd->texture_create(texture_format, texture_view);
	if (!mask_prefilter.filtered_depth_rd_texture.is_valid()) {
		UtilityFunctions::printerr("[DepthMask] Failed to create the filtered environment depth texture.");
		_free_mask_prefilter_resources_rt();
		mask_prefilter.initialization_failed = true;
		return false;
	}

	mask_prefilter.filtered_depth_texture = rs->texture_rd_create(
			mask_prefilter.filtered_depth_rd_texture,
			RenderingServer::TEXTURE_LAYERED_2D_ARRAY);
	if (!mask_prefilter.filtered_depth_texture.is_valid()) {
		UtilityFunctions::printerr("[DepthMask] Failed to expose the filtered depth texture to RenderingServer.");
		rd->free_rid(mask_prefilter.filtered_depth_rd_texture);
		mask_prefilter.filtered_depth_rd_texture = RID();
		_free_mask_prefilter_resources_rt();
		mask_prefilter.initialization_failed = true;
		return false;
	}

	mask_prefilter.mask_rd_texture = mask_rd_texture;
	mask_prefilter.texture_size = depth_size;
	mask_prefilter.uniform_sets.resize(render_state.depth_swapchain_textures.size());
	mask_prefilter.last_depth_swapchain_index = UINT32_MAX;
	if (reprojection_material.is_valid()) {
		reprojection_material->set_shader_parameter("filtered_depth_texture", mask_prefilter.filtered_depth_texture);
	}
	return true;
#else
	return false;
#endif // ANDROID_ENABLED
}

void OpenXRMetaEnvironmentDepthExtension::_free_mask_prefilter_resources_rt() {
	RenderingServer *rs = RenderingServer::get_singleton();
	RenderingDevice *rd = rs ? rs->get_rendering_device() : nullptr;

	if (rd && depth_reprojection.filtered_depth_uniform_set.is_valid() &&
			rd->uniform_set_is_valid(depth_reprojection.filtered_depth_uniform_set)) {
		rd->free_rid(depth_reprojection.filtered_depth_uniform_set);
	}
	depth_reprojection.filtered_depth_uniform_set = RID();

	if (reprojection_material.is_valid()) {
		reprojection_material->set_shader_parameter("mask_filter_mode", reprojection_mask_filter_enabled ? 2 : 0);
		reprojection_material->set_shader_parameter("filtered_depth_texture", RID());
	}
	if (rd) {
		for (const RID &uniform_set : mask_prefilter.uniform_sets) {
			if (uniform_set.is_valid() && rd->uniform_set_is_valid(uniform_set)) {
				rd->free_rid(uniform_set);
			}
		}
		if (mask_prefilter.pipeline.is_valid()) {
			rd->free_rid(mask_prefilter.pipeline);
		}
		if (mask_prefilter.shader.is_valid()) {
			rd->free_rid(mask_prefilter.shader);
		}
		if (mask_prefilter.sampler.is_valid()) {
			rd->free_rid(mask_prefilter.sampler);
		}
	}
	mask_prefilter.uniform_sets.clear();

	if (rs && mask_prefilter.filtered_depth_texture.is_valid()) {
		rs->free_rid(mask_prefilter.filtered_depth_texture);
	}
	// texture_rd_create() creates a shared RenderingServer view and deliberately
	// does not take ownership of the original RD texture.
	if (rd && mask_prefilter.filtered_depth_rd_texture.is_valid()) {
		rd->free_rid(mask_prefilter.filtered_depth_rd_texture);
	}

	mask_prefilter.sampler = RID();
	mask_prefilter.shader = RID();
	mask_prefilter.pipeline = RID();
	mask_prefilter.filtered_depth_rd_texture = RID();
	mask_prefilter.filtered_depth_texture = RID();
	mask_prefilter.mask_rd_texture = RID();
	mask_prefilter.texture_size = Vector2i();
	mask_prefilter.last_depth_swapchain_index = UINT32_MAX;
}

RID OpenXRMetaEnvironmentDepthExtension::_get_mask_prefilter_uniform_set_rt(uint32_t p_swapchain_index) {
#ifdef ANDROID_ENABLED
	ERR_FAIL_UNSIGNED_INDEX_V(p_swapchain_index, mask_prefilter.uniform_sets.size(), RID());
	RID &uniform_set = mask_prefilter.uniform_sets[p_swapchain_index];

	RenderingServer *rs = RenderingServer::get_singleton();
	ERR_FAIL_NULL_V(rs, RID());
	RenderingDevice *rd = rs->get_rendering_device();
	ERR_FAIL_NULL_V(rd, RID());
	if (uniform_set.is_valid() && rd->uniform_set_is_valid(uniform_set)) {
		return uniform_set;
	}

	const RID meta_depth_rd_texture = rs->texture_get_rd_texture(render_state.depth_swapchain_textures[p_swapchain_index]);
	if (!meta_depth_rd_texture.is_valid() || !mask_prefilter.mask_rd_texture.is_valid()) {
		return RID();
	}

	TypedArray<Ref<RDUniform>> uniforms;

	Ref<RDUniform> meta_depth_uniform;
	meta_depth_uniform.instantiate();
	meta_depth_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
	meta_depth_uniform->set_binding(0);
	meta_depth_uniform->add_id(mask_prefilter.sampler);
	meta_depth_uniform->add_id(meta_depth_rd_texture);
	uniforms.push_back(meta_depth_uniform);

	Ref<RDUniform> mask_depth_uniform;
	mask_depth_uniform.instantiate();
	mask_depth_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
	mask_depth_uniform->set_binding(1);
	mask_depth_uniform->add_id(mask_prefilter.sampler);
	mask_depth_uniform->add_id(mask_prefilter.mask_rd_texture);
	uniforms.push_back(mask_depth_uniform);

	Ref<RDUniform> output_uniform;
	output_uniform.instantiate();
	output_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
	output_uniform->set_binding(2);
	output_uniform->add_id(mask_prefilter.filtered_depth_rd_texture);
	uniforms.push_back(output_uniform);

	uniform_set = rd->uniform_set_create(uniforms, mask_prefilter.shader, 0);
	return uniform_set;
#else
	return RID();
#endif // ANDROID_ENABLED
}

bool OpenXRMetaEnvironmentDepthExtension::_dispatch_mask_prefilter_rt(uint32_t p_swapchain_index) {
#ifdef ANDROID_ENABLED
	if (!_ensure_mask_prefilter_resources_rt()) {
		if (reprojection_material.is_valid()) {
			reprojection_material->set_shader_parameter("mask_filter_mode", 2);
		}
		return false;
	}

	RenderingServer *rs = RenderingServer::get_singleton();
	ERR_FAIL_NULL_V(rs, false);
	RenderingDevice *rd = rs->get_rendering_device();
	ERR_FAIL_NULL_V(rd, false);
	const RID uniform_set = _get_mask_prefilter_uniform_set_rt(p_swapchain_index);
	if (!uniform_set.is_valid()) {
		UtilityFunctions::printerr("[DepthMask] Failed to create the GPU prefilter uniform set.");
		return false;
	}

	const int64_t compute_list = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(compute_list, mask_prefilter.pipeline);
	rd->compute_list_bind_uniform_set(compute_list, uniform_set, 0);
	rd->compute_list_dispatch(
			compute_list,
			(mask_prefilter.texture_size.x + 7) / 8,
			(mask_prefilter.texture_size.y + 7) / 8,
			2);
	rd->compute_list_end();

	mask_prefilter.last_depth_swapchain_index = p_swapchain_index;
	if (reprojection_material.is_valid()) {
		reprojection_material->set_shader_parameter("filtered_depth_texture", mask_prefilter.filtered_depth_texture);
		reprojection_material->set_shader_parameter("mask_filter_mode", 1);
	}
	return true;
#else
	return false;
#endif // ANDROID_ENABLED
}

static void store_projection_std140(float *p_target, const Projection &p_projection) {
	for (int column = 0; column < 4; column++) {
		for (int row = 0; row < 4; row++) {
			p_target[column * 4 + row] = static_cast<float>(p_projection[column][row]);
		}
	}
}

bool OpenXRMetaEnvironmentDepthExtension::_ensure_depth_reprojection_resources_rt() {
#ifdef ANDROID_ENABLED
	if (depth_reprojection.initialization_failed) {
		return false;
	}
	if (render_state.graphics_api != GRAPHICS_API_VULKAN ||
			render_state.depth_swapchain_textures.is_empty()) {
		return false;
	}

	RenderingServer *rs = RenderingServer::get_singleton();
	ERR_FAIL_NULL_V(rs, false);
	RenderingDevice *rd = rs->get_rendering_device();
	ERR_FAIL_NULL_V(rd, false);

	const Vector2i depth_size(depth_swapchain_width.load(), depth_swapchain_height.load());
	if (depth_size.x <= 0 || depth_size.y <= 0) {
		return false;
	}
	if (depth_reprojection.reprojected_depth_texture.is_valid() &&
			(depth_reprojection.texture_size != depth_size ||
					depth_reprojection.raw_depth_uniform_sets.size() != render_state.depth_swapchain_textures.size())) {
		_free_depth_reprojection_resources_rt();
	}
	if (depth_reprojection.reprojected_depth_texture.is_valid()) {
		return true;
	}

	const BitField<RenderingDevice::TextureUsageBits> texture_usage =
			RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
			RenderingDevice::TEXTURE_USAGE_STORAGE_BIT;
	RenderingDevice::DataFormat output_format = RenderingDevice::DATA_FORMAT_R16_UNORM;
	String output_image_format = "r16";
	if (!rd->texture_is_format_supported_for_usage(output_format, texture_usage)) {
		output_format = RenderingDevice::DATA_FORMAT_R32_SFLOAT;
		output_image_format = "r32f";
	}
	if (!rd->texture_is_format_supported_for_usage(output_format, texture_usage)) {
		UtilityFunctions::printerr("[EnvironmentDepth] No sampleable storage texture format is available for low-resolution reprojection.");
		depth_reprojection.initialization_failed = true;
		return false;
	}

	Ref<RDSamplerState> sampler_state;
	sampler_state.instantiate();
	sampler_state->set_mag_filter(RenderingDevice::SAMPLER_FILTER_NEAREST);
	sampler_state->set_min_filter(RenderingDevice::SAMPLER_FILTER_NEAREST);
	sampler_state->set_mip_filter(RenderingDevice::SAMPLER_FILTER_NEAREST);
	sampler_state->set_repeat_u(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
	sampler_state->set_repeat_v(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
	sampler_state->set_repeat_w(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
	depth_reprojection.sampler = rd->sampler_create(sampler_state);
	if (!depth_reprojection.sampler.is_valid()) {
		UtilityFunctions::printerr("[EnvironmentDepth] Failed to create the low-resolution reprojection sampler.");
		depth_reprojection.initialization_failed = true;
		return false;
	}

	String compute_shader_code = META_ENVIRONMENT_DEPTH_LOW_RES_REPROJECTION_SHADER_CODE;
	compute_shader_code = compute_shader_code.replace("OUTPUT_IMAGE_FORMAT", output_image_format);
	if (reprojection_bilinear_filtering) {
		compute_shader_code = compute_shader_code.replace("//DEFINES", "#define USE_BILINEAR_FILTERING");
	} else {
		compute_shader_code = compute_shader_code.replace("//DEFINES", "");
	}

	Ref<RDShaderSource> shader_source;
	shader_source.instantiate();
	shader_source->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
	shader_source->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE, compute_shader_code);
	Ref<RDShaderSPIRV> shader_spirv = rd->shader_compile_spirv_from_source(shader_source);
	if (shader_spirv.is_null()) {
		UtilityFunctions::printerr("[EnvironmentDepth] Low-resolution reprojection shader compilation returned no SPIR-V.");
		_free_depth_reprojection_resources_rt();
		depth_reprojection.initialization_failed = true;
		return false;
	}
	const String compile_error = shader_spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
	if (!compile_error.is_empty()) {
		UtilityFunctions::printerr("[EnvironmentDepth] Low-resolution reprojection shader compilation failed:\n", compile_error);
		_free_depth_reprojection_resources_rt();
		depth_reprojection.initialization_failed = true;
		return false;
	}

	depth_reprojection.shader = rd->shader_create_from_spirv(shader_spirv, "Meta environment depth low-resolution reprojection");
	if (!depth_reprojection.shader.is_valid()) {
		UtilityFunctions::printerr("[EnvironmentDepth] Failed to create the low-resolution reprojection shader.");
		_free_depth_reprojection_resources_rt();
		depth_reprojection.initialization_failed = true;
		return false;
	}
	depth_reprojection.pipeline = rd->compute_pipeline_create(depth_reprojection.shader);
	if (!depth_reprojection.pipeline.is_valid()) {
		UtilityFunctions::printerr("[EnvironmentDepth] Failed to create the low-resolution reprojection pipeline.");
		_free_depth_reprojection_resources_rt();
		depth_reprojection.initialization_failed = true;
		return false;
	}

	Ref<RDTextureFormat> texture_format;
	texture_format.instantiate();
	texture_format->set_format(output_format);
	texture_format->set_width(depth_size.x);
	texture_format->set_height(depth_size.y);
	texture_format->set_depth(1);
	texture_format->set_array_layers(2);
	texture_format->set_mipmaps(1);
	texture_format->set_texture_type(RenderingDevice::TEXTURE_TYPE_2D_ARRAY);
	texture_format->set_samples(RenderingDevice::TEXTURE_SAMPLES_1);
	texture_format->set_usage_bits(texture_usage);

	Ref<RDTextureView> texture_view;
	texture_view.instantiate();
	depth_reprojection.reprojected_depth_rd_texture = rd->texture_create(texture_format, texture_view);
	if (!depth_reprojection.reprojected_depth_rd_texture.is_valid()) {
		UtilityFunctions::printerr("[EnvironmentDepth] Failed to create the low-resolution reprojected depth texture.");
		_free_depth_reprojection_resources_rt();
		depth_reprojection.initialization_failed = true;
		return false;
	}

	depth_reprojection.reprojected_depth_texture = rs->texture_rd_create(
			depth_reprojection.reprojected_depth_rd_texture,
			RenderingServer::TEXTURE_LAYERED_2D_ARRAY);
	if (!depth_reprojection.reprojected_depth_texture.is_valid()) {
		UtilityFunctions::printerr("[EnvironmentDepth] Failed to expose the low-resolution reprojected depth texture.");
		_free_depth_reprojection_resources_rt();
		depth_reprojection.initialization_failed = true;
		return false;
	}

	PackedByteArray initial_parameters;
	initial_parameters.resize(68 * sizeof(float));
	depth_reprojection.parameters_buffer = rd->uniform_buffer_create(initial_parameters.size(), initial_parameters);
	if (!depth_reprojection.parameters_buffer.is_valid()) {
		UtilityFunctions::printerr("[EnvironmentDepth] Failed to create the low-resolution reprojection parameter buffer.");
		_free_depth_reprojection_resources_rt();
		depth_reprojection.initialization_failed = true;
		return false;
	}

	depth_reprojection.texture_size = depth_size;
	depth_reprojection.raw_depth_uniform_sets.resize(render_state.depth_swapchain_textures.size());
	reprojection_material_dirty = true;
	if (reprojection_material.is_valid()) {
		reprojection_material->set_shader_parameter("reprojected_depth_texture", depth_reprojection.reprojected_depth_texture);
	}
	return true;
#else
	return false;
#endif // ANDROID_ENABLED
}

void OpenXRMetaEnvironmentDepthExtension::_free_depth_reprojection_resources_rt() {
	RenderingServer *rs = RenderingServer::get_singleton();
	RenderingDevice *rd = rs ? rs->get_rendering_device() : nullptr;
	const bool had_reprojected_texture = depth_reprojection.reprojected_depth_texture.is_valid();

	if (rd) {
		for (const RID &uniform_set : depth_reprojection.raw_depth_uniform_sets) {
			if (uniform_set.is_valid() && rd->uniform_set_is_valid(uniform_set)) {
				rd->free_rid(uniform_set);
			}
		}
		if (depth_reprojection.filtered_depth_uniform_set.is_valid() &&
				rd->uniform_set_is_valid(depth_reprojection.filtered_depth_uniform_set)) {
			rd->free_rid(depth_reprojection.filtered_depth_uniform_set);
		}
		if (depth_reprojection.parameters_buffer.is_valid()) {
			rd->free_rid(depth_reprojection.parameters_buffer);
		}
		if (depth_reprojection.pipeline.is_valid()) {
			rd->free_rid(depth_reprojection.pipeline);
		}
		if (depth_reprojection.shader.is_valid()) {
			rd->free_rid(depth_reprojection.shader);
		}
		if (depth_reprojection.sampler.is_valid()) {
			rd->free_rid(depth_reprojection.sampler);
		}
	}
	depth_reprojection.raw_depth_uniform_sets.clear();

	if (rs && depth_reprojection.reprojected_depth_texture.is_valid()) {
		rs->free_rid(depth_reprojection.reprojected_depth_texture);
	}
	if (rd && depth_reprojection.reprojected_depth_rd_texture.is_valid()) {
		rd->free_rid(depth_reprojection.reprojected_depth_rd_texture);
	}

	depth_reprojection.sampler = RID();
	depth_reprojection.shader = RID();
	depth_reprojection.pipeline = RID();
	depth_reprojection.parameters_buffer = RID();
	depth_reprojection.reprojected_depth_rd_texture = RID();
	depth_reprojection.reprojected_depth_texture = RID();
	depth_reprojection.filtered_depth_uniform_set = RID();
	depth_reprojection.texture_size = Vector2i();
	if (had_reprojected_texture) {
		reprojection_material_dirty = true;
	}
}

RID OpenXRMetaEnvironmentDepthExtension::_get_depth_reprojection_uniform_set_rt(uint32_t p_swapchain_index, bool p_use_filtered_depth) {
#ifdef ANDROID_ENABLED
	ERR_FAIL_UNSIGNED_INDEX_V(p_swapchain_index, depth_reprojection.raw_depth_uniform_sets.size(), RID());

	RenderingServer *rs = RenderingServer::get_singleton();
	ERR_FAIL_NULL_V(rs, RID());
	RenderingDevice *rd = rs->get_rendering_device();
	ERR_FAIL_NULL_V(rd, RID());

	RID *uniform_set = p_use_filtered_depth ?
			&depth_reprojection.filtered_depth_uniform_set :
			&depth_reprojection.raw_depth_uniform_sets[p_swapchain_index];
	if (uniform_set->is_valid() && rd->uniform_set_is_valid(*uniform_set)) {
		return *uniform_set;
	}

	RID source_depth_rd_texture;
	if (p_use_filtered_depth) {
		source_depth_rd_texture = mask_prefilter.filtered_depth_rd_texture;
	} else {
		source_depth_rd_texture = rs->texture_get_rd_texture(render_state.depth_swapchain_textures[p_swapchain_index]);
	}
	if (!source_depth_rd_texture.is_valid()) {
		return RID();
	}

	TypedArray<Ref<RDUniform>> uniforms;

	Ref<RDUniform> source_depth_uniform;
	source_depth_uniform.instantiate();
	source_depth_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
	source_depth_uniform->set_binding(0);
	source_depth_uniform->add_id(depth_reprojection.sampler);
	source_depth_uniform->add_id(source_depth_rd_texture);
	uniforms.push_back(source_depth_uniform);

	Ref<RDUniform> output_uniform;
	output_uniform.instantiate();
	output_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
	output_uniform->set_binding(1);
	output_uniform->add_id(depth_reprojection.reprojected_depth_rd_texture);
	uniforms.push_back(output_uniform);

	Ref<RDUniform> parameters_uniform;
	parameters_uniform.instantiate();
	parameters_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_UNIFORM_BUFFER);
	parameters_uniform->set_binding(2);
	parameters_uniform->add_id(depth_reprojection.parameters_buffer);
	uniforms.push_back(parameters_uniform);

	*uniform_set = rd->uniform_set_create(uniforms, depth_reprojection.shader, 0);
	return *uniform_set;
#else
	return RID();
#endif // ANDROID_ENABLED
}

void OpenXRMetaEnvironmentDepthExtension::_dispatch_depth_reprojection_rt(uint32_t p_swapchain_index, bool p_use_filtered_depth) {
#ifdef ANDROID_ENABLED
	if (!_ensure_depth_reprojection_resources_rt()) {
		return;
	}

	RenderingServer *rs = RenderingServer::get_singleton();
	ERR_FAIL_NULL(rs);
	RenderingDevice *rd = rs->get_rendering_device();
	ERR_FAIL_NULL(rd);
	const RID uniform_set = _get_depth_reprojection_uniform_set_rt(p_swapchain_index, p_use_filtered_depth);
	if (!uniform_set.is_valid()) {
		UtilityFunctions::printerr("[EnvironmentDepth] Failed to create the low-resolution reprojection uniform set.");
		_free_depth_reprojection_resources_rt();
		depth_reprojection.initialization_failed = true;
		return;
	}

	PackedByteArray parameter_data;
	parameter_data.resize(68 * sizeof(float));
	float *parameters = reinterpret_cast<float *>(parameter_data.ptrw());
	store_projection_std140(parameters, render_state.camera_to_depth[0]);
	store_projection_std140(parameters + 16, render_state.camera_to_depth[1]);
	store_projection_std140(parameters + 32, render_state.depth_to_camera[0]);
	store_projection_std140(parameters + 48, render_state.depth_to_camera[1]);
	parameters[64] = reprojection_offset_scale;
	parameters[65] = reprojection_offset_exponent;
	parameters[66] = reprojection_offset_exponent != 1.0f ? 1.0f : 0.0f;
	parameters[67] = 0.0f;
	const Error update_error = rd->buffer_update(
			depth_reprojection.parameters_buffer,
			0,
			parameter_data.size(),
			parameter_data);
	if (update_error != OK) {
		UtilityFunctions::printerr("[EnvironmentDepth] Failed to update the low-resolution reprojection parameters.");
		_free_depth_reprojection_resources_rt();
		depth_reprojection.initialization_failed = true;
		return;
	}

	const int64_t compute_list = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(compute_list, depth_reprojection.pipeline);
	rd->compute_list_bind_uniform_set(compute_list, uniform_set, 0);
	rd->compute_list_dispatch(
			compute_list,
			(depth_reprojection.texture_size.x + 7) / 8,
			(depth_reprojection.texture_size.y + 7) / 8,
			2);
	rd->compute_list_end();

	if (reprojection_material.is_valid()) {
		reprojection_material->set_shader_parameter("reprojected_depth_texture", depth_reprojection.reprojected_depth_texture);
	}
#endif // ANDROID_ENABLED
}

Vector2i OpenXRMetaEnvironmentDepthExtension::get_environment_depth_texture_size() const {
	return Vector2i(depth_swapchain_width.load(), depth_swapchain_height.load());
}

void OpenXRMetaEnvironmentDepthExtension::get_environment_depth_map_async(const Callable &p_callback) {
	RenderingServer *rs = RenderingServer::get_singleton();
	ERR_FAIL_NULL(rs);
	ERR_FAIL_COND(!depth_provider_started);
	rs->call_on_render_thread(callable_mp(this, &OpenXRMetaEnvironmentDepthExtension::_add_depth_map_callback_rt).bind(p_callback));
}

static void create_shader_global_uniform(const String &p_name, RenderingServer::GlobalShaderParameterType p_type, Variant p_value, RenderingServer *p_rendering_server, ProjectSettings *p_project_settings, bool p_is_editor) {
	String setting_name = "shader_globals/" + p_name;
	if (!p_project_settings->has_setting(setting_name)) {
		p_rendering_server->global_shader_parameter_add(p_name, p_type, p_value);
		if (p_is_editor) {
			String type_name;
			switch (p_type) {
				case RenderingServer::GLOBAL_VAR_TYPE_BOOL: {
					type_name = "bool";
				} break;
				case RenderingServer::GLOBAL_VAR_TYPE_SAMPLER2DARRAY: {
					type_name = "sampler2DArray";
				} break;
				case RenderingServer::GLOBAL_VAR_TYPE_VEC2: {
					type_name = "vec2";
				} break;
				case RenderingServer::GLOBAL_VAR_TYPE_MAT4: {
					type_name = "mat4";
				} break;
			}

			Variant setting_value = p_value;
			if (p_type == RenderingServer::GLOBAL_VAR_TYPE_SAMPLER2DARRAY) {
				// In ProjectSettings, this uses a path as a value.
				setting_value = "";
			}

			Dictionary d;
			d["type"] = type_name;
			d["value"] = setting_value;
			p_project_settings->set(setting_name, d);
		}
	}
}

static void remove_shader_global_uniform(const String &p_name, RenderingServer *p_rendering_server, ProjectSettings *p_project_settings) {
	String setting_name = "shader_globals/" + p_name;
	if (p_project_settings->has_setting(setting_name)) {
		p_rendering_server->global_shader_parameter_remove(p_name);
		p_project_settings->clear(setting_name);
	}
}

void OpenXRMetaEnvironmentDepthExtension::setup_global_uniforms() {
	RenderingServer *rs = RenderingServer::get_singleton();
	ERR_FAIL_NULL(rs);

	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	ERR_FAIL_NULL(project_settings);

	bool enabled = project_settings->get_setting_with_override("xr/openxr/extensions/meta/environment_depth");

	if (!enabled) {
		remove_shader_global_uniform(META_ENVIRONMENT_DEPTH_AVAILABLE_NAME, rs, project_settings);
		remove_shader_global_uniform(META_ENVIRONMENT_DEPTH_TEXTURE_NAME, rs, project_settings);
		remove_shader_global_uniform(META_ENVIRONMENT_DEPTH_TEXEL_SIZE_NAME, rs, project_settings);
		remove_shader_global_uniform(META_ENVIRONMENT_DEPTH_Z_BUFFER_PARAMS_NAME, rs, project_settings);
		remove_shader_global_uniform(META_ENVIRONMENT_DEPTH_PROJECTION_VIEW_LEFT_NAME, rs, project_settings);
		remove_shader_global_uniform(META_ENVIRONMENT_DEPTH_PROJECTION_VIEW_RIGHT_NAME, rs, project_settings);
		remove_shader_global_uniform(META_ENVIRONMENT_DEPTH_INV_PROJECTION_VIEW_LEFT_NAME, rs, project_settings);
		remove_shader_global_uniform(META_ENVIRONMENT_DEPTH_INV_PROJECTION_VIEW_RIGHT_NAME, rs, project_settings);
		remove_shader_global_uniform(META_ENVIRONMENT_DEPTH_FROM_CAMERA_PROJECTION_LEFT_NAME, rs, project_settings);
		remove_shader_global_uniform(META_ENVIRONMENT_DEPTH_FROM_CAMERA_PROJECTION_RIGHT_NAME, rs, project_settings);
		remove_shader_global_uniform(META_ENVIRONMENT_DEPTH_TO_CAMERA_PROJECTION_LEFT_NAME, rs, project_settings);
		remove_shader_global_uniform(META_ENVIRONMENT_DEPTH_TO_CAMERA_PROJECTION_RIGHT_NAME, rs, project_settings);

		already_setup_global_uniforms = false;
		return;
	}

	if (already_setup_global_uniforms) {
		return;
	}

	Engine *engine = Engine::get_singleton();
	ERR_FAIL_NULL(engine);

	bool is_editor = engine->is_editor_hint();

	// Set this right away, to prevent getting in a loop of project settings changes.
	already_setup_global_uniforms = true;

	create_shader_global_uniform(META_ENVIRONMENT_DEPTH_AVAILABLE_NAME, RenderingServer::GLOBAL_VAR_TYPE_BOOL, false, rs, project_settings, is_editor);
	create_shader_global_uniform(META_ENVIRONMENT_DEPTH_TEXTURE_NAME, RenderingServer::GLOBAL_VAR_TYPE_SAMPLER2DARRAY, Variant(), rs, project_settings, is_editor);
	create_shader_global_uniform(META_ENVIRONMENT_DEPTH_TEXEL_SIZE_NAME, RenderingServer::GLOBAL_VAR_TYPE_VEC2, Vector2(), rs, project_settings, is_editor);
	create_shader_global_uniform(META_ENVIRONMENT_DEPTH_Z_BUFFER_PARAMS_NAME, RenderingServer::GLOBAL_VAR_TYPE_VEC2, Vector2(-0.2, -1.0), rs, project_settings, is_editor);
	create_shader_global_uniform(META_ENVIRONMENT_DEPTH_PROJECTION_VIEW_LEFT_NAME, RenderingServer::GLOBAL_VAR_TYPE_MAT4, Projection(), rs, project_settings, is_editor);
	create_shader_global_uniform(META_ENVIRONMENT_DEPTH_PROJECTION_VIEW_RIGHT_NAME, RenderingServer::GLOBAL_VAR_TYPE_MAT4, Projection(), rs, project_settings, is_editor);
	create_shader_global_uniform(META_ENVIRONMENT_DEPTH_INV_PROJECTION_VIEW_LEFT_NAME, RenderingServer::GLOBAL_VAR_TYPE_MAT4, Projection(), rs, project_settings, is_editor);
	create_shader_global_uniform(META_ENVIRONMENT_DEPTH_INV_PROJECTION_VIEW_RIGHT_NAME, RenderingServer::GLOBAL_VAR_TYPE_MAT4, Projection(), rs, project_settings, is_editor);
	create_shader_global_uniform(META_ENVIRONMENT_DEPTH_FROM_CAMERA_PROJECTION_LEFT_NAME, RenderingServer::GLOBAL_VAR_TYPE_MAT4, Projection(), rs, project_settings, is_editor);
	create_shader_global_uniform(META_ENVIRONMENT_DEPTH_FROM_CAMERA_PROJECTION_RIGHT_NAME, RenderingServer::GLOBAL_VAR_TYPE_MAT4, Projection(), rs, project_settings, is_editor);
	create_shader_global_uniform(META_ENVIRONMENT_DEPTH_TO_CAMERA_PROJECTION_LEFT_NAME, RenderingServer::GLOBAL_VAR_TYPE_MAT4, Projection(), rs, project_settings, is_editor);
	create_shader_global_uniform(META_ENVIRONMENT_DEPTH_TO_CAMERA_PROJECTION_RIGHT_NAME, RenderingServer::GLOBAL_VAR_TYPE_MAT4, Projection(), rs, project_settings, is_editor);
}

bool OpenXRMetaEnvironmentDepthExtension::initialize_meta_environment_depth_extension(const XrInstance &p_instance) {
	GDEXTENSION_INIT_XR_FUNC_V(xrCreateEnvironmentDepthProviderMETA);
	GDEXTENSION_INIT_XR_FUNC_V(xrDestroyEnvironmentDepthProviderMETA);
	GDEXTENSION_INIT_XR_FUNC_V(xrStartEnvironmentDepthProviderMETA);
	GDEXTENSION_INIT_XR_FUNC_V(xrStopEnvironmentDepthProviderMETA);
	GDEXTENSION_INIT_XR_FUNC_V(xrCreateEnvironmentDepthSwapchainMETA);
	GDEXTENSION_INIT_XR_FUNC_V(xrDestroyEnvironmentDepthSwapchainMETA);
	GDEXTENSION_INIT_XR_FUNC_V(xrEnumerateEnvironmentDepthSwapchainImagesMETA);
	GDEXTENSION_INIT_XR_FUNC_V(xrGetEnvironmentDepthSwapchainStateMETA);
	GDEXTENSION_INIT_XR_FUNC_V(xrAcquireEnvironmentDepthImageMETA);
	GDEXTENSION_INIT_XR_FUNC_V(xrSetEnvironmentDepthHandRemovalMETA);

	return true;
}

void OpenXRMetaEnvironmentDepthExtension::cleanup() {
	meta_environment_depth_ext = false;
}

bool OpenXRMetaEnvironmentDepthExtension::_create_depth_provider_rt() {
#ifdef ANDROID_ENABLED
	if (!meta_environment_depth_ext) {
		UtilityFunctions::printerr("Environment depth not supported");
		return false;
	}
	if (render_state.graphics_api == GRAPHICS_API_UNKNOWN) {
		render_state.graphics_api = get_graphics_api();
	}
	if (render_state.graphics_api == GRAPHICS_API_UNSUPPORTED) {
		UtilityFunctions::printerr("Environment depth is not supported with the current graphics API");
		return false;
	}

	RenderingServer *rs = RenderingServer::get_singleton();
	ERR_FAIL_NULL_V(rs, false);

	XrResult result;

	XrEnvironmentDepthProviderCreateInfoMETA provider_create_info = {
		XR_TYPE_ENVIRONMENT_DEPTH_PROVIDER_CREATE_INFO_META, // type
		nullptr, // next
		0, // createFlags
	};

	result = xrCreateEnvironmentDepthProviderMETA(SESSION, &provider_create_info, &render_state.depth_provider);
	if (XR_FAILED(result)) {
		UtilityFunctions::printerr("Failed to create environment depth provider: ", get_openxr_api()->get_error_string(result));
		return false;
	}

	XrEnvironmentDepthSwapchainCreateInfoMETA swapchain_create_info = {
		XR_TYPE_ENVIRONMENT_DEPTH_SWAPCHAIN_CREATE_INFO_META, // type
		nullptr, // next
		0, // createFlags
	};

	result = xrCreateEnvironmentDepthSwapchainMETA(render_state.depth_provider, &swapchain_create_info, &render_state.depth_swapchain);
	if (XR_FAILED(result)) {
		UtilityFunctions::printerr("Failed to create environment depth swapchain: ", get_openxr_api()->get_error_string(result));
		_destroy_depth_provider_rt();
		return false;
	}

	XrEnvironmentDepthSwapchainStateMETA swapchain_state = {
		XR_TYPE_ENVIRONMENT_DEPTH_SWAPCHAIN_STATE_META, // type
		nullptr, // next
		0, // width
		0, // height
	};

	result = xrGetEnvironmentDepthSwapchainStateMETA(render_state.depth_swapchain, &swapchain_state);
	if (XR_FAILED(result)) {
		UtilityFunctions::printerr("Failed to get environment depth swapchain state: ", get_openxr_api()->get_error_string(result));
		_destroy_depth_provider_rt();
		return false;
	}
	if (swapchain_state.width == 0 || swapchain_state.height == 0) {
		UtilityFunctions::printerr("Environment depth runtime returned an invalid zero-sized swapchain");
		_destroy_depth_provider_rt();
		return false;
	}

	render_state.depth_swapchain_texel_size = Vector2(1.0 / swapchain_state.width, 1.0 / swapchain_state.height);
	depth_swapchain_width.store(swapchain_state.width);
	depth_swapchain_height.store(swapchain_state.height);

	uint32_t swapchain_length = 0;

	result = xrEnumerateEnvironmentDepthSwapchainImagesMETA(render_state.depth_swapchain, swapchain_length, &swapchain_length, nullptr);
	if (XR_FAILED(result)) {
		UtilityFunctions::printerr("Failed to get environment depth swapchain image count: ", get_openxr_api()->get_error_string(result));
		_destroy_depth_provider_rt();
		return false;
	}
	if (swapchain_length == 0) {
		UtilityFunctions::printerr("Environment depth runtime returned an empty swapchain");
		_destroy_depth_provider_rt();
		return false;
	}

	if (render_state.graphics_api == GRAPHICS_API_OPENGL) {
		LocalVector<XrSwapchainImageOpenGLESKHR> swapchain_images;

		swapchain_images.resize(swapchain_length);
		for (auto &image : swapchain_images) {
			image.type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
			image.next = nullptr;
			image.image = 0;
		}

		result = xrEnumerateEnvironmentDepthSwapchainImagesMETA(render_state.depth_swapchain, swapchain_length, &swapchain_length, (XrSwapchainImageBaseHeader *)swapchain_images.ptr());
		if (XR_FAILED(result)) {
			UtilityFunctions::printerr("Failed to get environment depth swapchain images: ", get_openxr_api()->get_error_string(result));
			_destroy_depth_provider_rt();
			return false;
		}

		render_state.depth_swapchain_textures.reserve(swapchain_length);

		for (const auto &image : swapchain_images) {
			RID texture = rs->texture_create_from_native_handle(
					RenderingServer::TextureType::TEXTURE_TYPE_LAYERED,
					Image::Format::FORMAT_RH, // GL_DEPTH_COMPONENT16
					image.image,
					swapchain_state.width,
					swapchain_state.height,
					1,
					2,
					RenderingServer::TextureLayeredType::TEXTURE_LAYERED_2D_ARRAY);

			if (!texture.is_valid()) {
				UtilityFunctions::printerr("Failed to import an OpenGL environment depth swapchain image");
				_destroy_depth_provider_rt();
				return false;
			}
			render_state.depth_swapchain_textures.push_back(texture);
		}
	} else if (render_state.graphics_api == GRAPHICS_API_VULKAN) {
		LocalVector<XrSwapchainImageVulkanKHR> swapchain_images;

		swapchain_images.resize(swapchain_length);
		for (auto &image : swapchain_images) {
			image.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR;
			image.next = nullptr;
			image.image = 0;
		}

		result = xrEnumerateEnvironmentDepthSwapchainImagesMETA(render_state.depth_swapchain, swapchain_length, &swapchain_length, (XrSwapchainImageBaseHeader *)swapchain_images.ptr());
		if (XR_FAILED(result)) {
			UtilityFunctions::printerr("Failed to get environment depth swapchain images: ", get_openxr_api()->get_error_string(result));
			_destroy_depth_provider_rt();
			return false;
		}

		render_state.depth_swapchain_textures.reserve(swapchain_length);
		render_state.depth_swapchain_rd_textures.reserve(swapchain_length);

		RenderingDevice *rendering_device = RenderingServer::get_singleton()->get_rendering_device();
		if (rendering_device == nullptr) {
			UtilityFunctions::printerr("RenderingDevice is unavailable while importing the environment depth swapchain");
			_destroy_depth_provider_rt();
			return false;
		}

		for (const auto &image : swapchain_images) {
			RID rd_texture = rendering_device->texture_create_from_extension(
					RenderingDevice::TEXTURE_TYPE_2D_ARRAY,
					RenderingDevice::DATA_FORMAT_D16_UNORM,
					RenderingDevice::TEXTURE_SAMPLES_1,
					RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT | RenderingDevice::TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
					reinterpret_cast<uint64_t>(image.image),
					swapchain_state.width,
					swapchain_state.height,
					1,
					2);

			if (!rd_texture.is_valid()) {
				UtilityFunctions::printerr("Failed to import a Vulkan environment depth swapchain image");
				_destroy_depth_provider_rt();
				return false;
			}
			render_state.depth_swapchain_rd_textures.push_back(rd_texture);

			RID texture = rs->texture_rd_create(rd_texture, RenderingServer::TextureLayeredType::TEXTURE_LAYERED_2D_ARRAY);
			if (!texture.is_valid()) {
				UtilityFunctions::printerr("Failed to create a RenderingServer view for a Vulkan environment depth image");
				_destroy_depth_provider_rt();
				return false;
			}
			render_state.depth_swapchain_textures.push_back(texture);
		}
	}

	return true;
#else
	return false;
#endif // ANDROID_ENABLED
}

OpenXRMetaEnvironmentDepthExtension::GraphicsAPI OpenXRMetaEnvironmentDepthExtension::get_graphics_api() {
	RenderingServer *rs = RenderingServer::get_singleton();
	ERR_FAIL_NULL_V(rs, GRAPHICS_API_UNKNOWN);

	String rendering_driver = rs->get_current_rendering_driver_name();
	if (rendering_driver.contains("opengl")) {
		return GRAPHICS_API_OPENGL;
	} else if (rendering_driver == "vulkan") {
		return GRAPHICS_API_VULKAN;
	}

	return GRAPHICS_API_UNSUPPORTED;
}

void OpenXRMetaEnvironmentDepthExtension::_start_environment_depth_rt() {
	ERR_FAIL_COND(render_state.depth_provider_started);

	if (render_state.depth_provider == XR_NULL_HANDLE) {
		if (!_create_depth_provider_rt()) {
			callable_mp(this, &OpenXRMetaEnvironmentDepthExtension::_notify_environment_depth_start_failed).call_deferred();
			return;
		}
	}

	XrResult result = xrStartEnvironmentDepthProviderMETA(render_state.depth_provider);
	if (XR_FAILED(result)) {
		UtilityFunctions::printerr("Failed to start environment depth provider: ", get_openxr_api()->get_error_string(result));
		callable_mp(this, &OpenXRMetaEnvironmentDepthExtension::_notify_environment_depth_start_failed).call_deferred();
		return;
	}

	render_state.depth_provider_started = true;
}

void OpenXRMetaEnvironmentDepthExtension::_stop_environment_depth_rt() {
	ERR_FAIL_COND(!render_state.depth_provider_started);
	ERR_FAIL_NULL(render_state.depth_provider);

	XrResult result = xrStopEnvironmentDepthProviderMETA(render_state.depth_provider);
	if (XR_FAILED(result)) {
		UtilityFunctions::printerr("Failed to stop environment depth provider: ", get_openxr_api()->get_error_string(result));
		return;
	}

	render_state.depth_provider_started = false;
}

void OpenXRMetaEnvironmentDepthExtension::_set_hand_removal_enabled_rt(bool p_enable) {
	if (render_state.depth_provider == XR_NULL_HANDLE) {
		if (!_create_depth_provider_rt()) {
			return;
		}
	}

	XrEnvironmentDepthHandRemovalSetInfoMETA info = {
		XR_TYPE_ENVIRONMENT_DEPTH_HAND_REMOVAL_SET_INFO_META, // type
		nullptr, // next
		p_enable, // enabled
	};

	XrResult result = xrSetEnvironmentDepthHandRemovalMETA(render_state.depth_provider, &info);
	if (XR_FAILED(result)) {
		UtilityFunctions::printerr("Failed to set hand removal enabled: ", get_openxr_api()->get_error_string(result));
		return;
	}
}

void OpenXRMetaEnvironmentDepthExtension::_add_depth_map_callback_rt(const Callable &p_callback) {
	render_state.depth_map_callbacks.push_back(p_callback);
}

void OpenXRMetaEnvironmentDepthExtension::_notify_environment_depth_start_failed() {
	if (!depth_provider_started) {
		return;
	}
	depth_provider_started = false;
	emit_signal("openxr_meta_environment_depth_stopped");
}

void OpenXRMetaEnvironmentDepthExtension::_destroy_depth_provider_rt() {
	_free_depth_reprojection_resources_rt();
	_free_mask_prefilter_resources_rt();
	depth_reprojection.initialization_failed = false;
	mask_prefilter.initialization_failed = false;
	render_state.current_depth_swapchain_index = -1;
	render_state.depth_reprojection_pending = false;
	_set_depth_globals_unavailable_rt();

	if (render_state.depth_provider_started) {
		_stop_environment_depth_rt();
	}

	// Release Godot's views before destroying the runtime-owned swapchain.
	// texture_rd_create() owns a shared view, while the original RD RID owns
	// the VkImageView created around the OpenXR VkImage.
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs) {
		for (const RID &texture : render_state.depth_swapchain_textures) {
			if (texture.is_valid()) {
				rs->free_rid(texture);
			}
		}
	}
	render_state.depth_swapchain_textures.clear();

	if (!render_state.depth_swapchain_rd_textures.is_empty() && rs) {
		RenderingDevice *rd = rs->get_rendering_device();
		if (rd) {
			for (const RID &rd_texture : render_state.depth_swapchain_rd_textures) {
				if (rd_texture.is_valid() && rd->texture_is_valid(rd_texture)) {
					rd->free_rid(rd_texture);
				}
			}
		}
	}
	render_state.depth_swapchain_rd_textures.clear();

	if (render_state.depth_swapchain != XR_NULL_HANDLE) {
		XrResult result = xrDestroyEnvironmentDepthSwapchainMETA(render_state.depth_swapchain);
		if (XR_FAILED(result)) {
			UtilityFunctions::printerr("Failed to destroy environment depth swapchain: ", get_openxr_api()->get_error_string(result));
		}
		render_state.depth_swapchain = XR_NULL_HANDLE;
	}

	depth_swapchain_width.store(0);
	depth_swapchain_height.store(0);

	if (render_state.depth_provider != XR_NULL_HANDLE) {
		XrResult result = xrDestroyEnvironmentDepthProviderMETA(render_state.depth_provider);
		if (XR_FAILED(result)) {
			UtilityFunctions::printerr("Failed to destroy environment depth provider: ", get_openxr_api()->get_error_string(result));
		}
		render_state.depth_provider = XR_NULL_HANDLE;
	}

	// Reset state on the main thread.
	callable_mp(this, &OpenXRMetaEnvironmentDepthExtension::reset_state).call_deferred();
}

void OpenXRMetaEnvironmentDepthExtension::reset_state() {
	const bool was_started = depth_provider_started;
	depth_provider_started = false;
	hand_removal_enabled = false;
	if (was_started) {
		emit_signal("openxr_meta_environment_depth_stopped");
	}
}
