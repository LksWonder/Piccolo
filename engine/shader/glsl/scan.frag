#version 310 es

#extension GL_GOOGLE_include_directive : enable

#include "constants.h"

layout(input_attachment_index = 0, set = 0, binding = 0) uniform highp subpassInput in_color;

layout(set = 0, binding = 1)  uniform _scan_info
{
    uint scan_distance;
    uint place_holder;
    highp float z_far;
    highp float z_near;
};

layout(set = 0, binding = 2) uniform sampler2D normal_sampler;
layout(set = 0, binding = 3) uniform sampler2D depth_sampler;

layout(location = 0) in highp vec2 in_uv;

layout(location = 0) out highp vec4 out_color;


highp float Linear01Depth(highp float depth, highp float near, highp float far)
{
    // 线性深度，即物体实际深度。裁剪空间的z是非线性，0-1.
    return near / (far - near) * (1.0 / (1.0 - depth)) - (near / (far - near));
}

void main()
{
    highp vec3 color = subpassLoad(in_color).rgb;

    highp float depth_color = texture(depth_sampler, in_uv).r;
    highp float linear_depth = Linear01Depth(depth_color, z_near, z_far);
    // out_color = vec4(depth_color);
    out_color = vec4(color.r, color.g * float(scan_distance), color.b, 1.0f);
}

