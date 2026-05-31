#ifndef SHADERS_H
#define SHADERS_H

static const char *vertex_shader_source =
    "#version 120\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
    "    v_texcoord = gl_MultiTexCoord0.xy;\n"
    "}\n";

static const char *fragment_shader_source =
    "#version 120\n"
    "uniform sampler2D tex;\n"
    "uniform float alpha;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "    vec4 color = texture2D(tex, v_texcoord);\n"
    "    gl_FragColor = color * alpha;\n"
    "}\n";

// A simple blur shader for background blur
static const char *blur_fragment_shader_source __attribute__((unused)) =
    "#version 120\n"
    "uniform sampler2D tex;\n"
    "uniform vec2 resolution;\n"
    "uniform float radius;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "    vec4 sum = vec4(0.0);\n"
    "    vec2 tex_offset = 1.0 / resolution;\n"
    "    float total = 0.0;\n"
    "    for (float x = -radius; x <= radius; x += 1.0) {\n"
    "        for (float y = -radius; y <= radius; y += 1.0) {\n"
    "            vec2 offset = vec2(x, y) * tex_offset;\n"
    "            sum += texture2D(tex, v_texcoord + offset);\n"
    "            total += 1.0;\n"
    "        }\n"
    "    }\n"
    "    gl_FragColor = sum / total;\n"
    "}\n";

#endif
