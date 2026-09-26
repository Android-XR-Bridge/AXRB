#pragma once

#include <string>
#include <vector>

/* Translates the guest's GLSL ES 3.00 into HLSL that D3DCompile accepts.
   The guest's shader is the authority. A construct this does not cover names
   itself in the error, which is the row that belongs in compat.md. */

struct GlslUniform {
    std::string name;
    int reg = 0;       /* constant register, so the driver writes at reg * 16 */
    int registers = 1; /* how many this uniform occupies */
    bool integer = false; /* an int uniform is read as bits, not as a float */
};

/* A vertex attribute. The driver builds the input layout from these and from
   what glVertexAttribPointer said the data looks like. */
struct GlslAttribute {
    std::string name;
    int location = 0;
    int components = 4;
};

/* A sampler is not a constant, so it gets a texture slot instead of a register. */
struct GlslSampler {
    std::string name;
    int slot = 0;
};

struct GlslProgram {
    std::string vertex;
    std::string fragment;
    std::vector<GlslUniform> uniforms;
    std::vector<GlslSampler> samplers;
    std::vector<GlslAttribute> attributes;
    std::string vertex_entry;
    int registers = 0;
    std::string error;
};

/* Both stages at once: a uniform has to land on the same register in each. */
bool glsl_translate(const std::string& vertex_source, const std::string& fragment_source, GlslProgram& out);
