#version 310 es

#extension GL_GOOGLE_include_directive : enable

#include "constants.h"

layout(input_attachment_index = 0, set = 0, binding = 0) uniform highp subpassInput in_color;

layout(set = 0, binding = 1)  uniform _scan_info
{
    uint scan_distance;
    uint place_holder;
};

layout(set = 0, binding = 2) uniform sampler2D normal_sampler;
layout(set = 0, binding = 3) uniform sampler2D depth_sampler;

layout(location = 0) in highp vec2 in_uv;

layout(location = 0) out highp vec4 out_color;


void main()
{
    highp vec3 color = subpassLoad(in_color).rgb;

    highp vec4 depth_color = texture(depth_sampler, in_uv);
    out_color = vec4(depth_color.r, depth_color.g * float(scan_distance), depth_color.b, 1.0f);
}

