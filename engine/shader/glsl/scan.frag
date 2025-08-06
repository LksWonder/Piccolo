#version 310 es

#extension GL_GOOGLE_include_directive : enable

#include "constants.h"

layout(input_attachment_index = 0, set = 0, binding = 0) uniform highp subpassInput in_color;
layout(input_attachment_index = 1, set = 0, binding = 1) uniform highp subpassInput in_depth_attachment;

layout(set = 0, binding = 2)  uniform _scan_info
{
    uint scan_distance;
    uint place_holder;
    highp float z_far;
    highp float z_near;
};

//layout(set = 0, binding = 3) uniform sampler2D normal_sampler;
//layout(set = 0, binding = 4) uniform sampler2D depth_sampler;

layout(location = 0) in highp vec2 in_uv;

layout(location = 0) out highp vec4 out_color;


highp float Linear01Depth(highp float depth, highp float near, highp float far)
{
    // 物体实际深度。裁剪空间的z是非线性，0-1.
    return near / (far - near) * (1.0 / (1.0 - depth)) - (near / (far - near));
    // 将深度值映射到0到1的范围
    // highp float linearDepth = (depth - near) / (far - near);
    // return linearDepth;
}

void main()
{
    highp vec3 color = subpassLoad(in_color).rgb;
    highp float depth_attacment_color = subpassLoad(in_depth_attachment).r;

    //highp vec4 normal_color = texture(normal_sampler, in_uv);
    //highp float depth_color = texture(depth_sampler, in_uv).r;
    //highp float linear_depth = Linear01Depth(depth_color, z_near, z_far);
    
    //通过像素深度是否在扫描的一定范围
    //if(linear_depth < float(scan_distance) && linear_depth > float(scan_distance) - 0.1f / z_far && linear_depth < 1.0f)
    //{
    //    //做一个渐变效果
    //    highp float scan_percent = 1.0f - (float(scan_distance) - linear_depth) / (0.1 / z_far);
    //    out_color =  mix(vec4(color, 1), vec4(1, 1, 0, 1), scan_percent);
    //    return;
    //}


    // out_color = vec4(vec3(depth_attacment_color), 1.0);
    out_color = vec4(color.r, color.g, color.b, 1.0f); //  * float(scan_distance)
}

