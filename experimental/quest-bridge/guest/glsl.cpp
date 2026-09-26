#include "glsl.h"

#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace {

struct Token {
    enum Kind { Id, Number, Punct, Space } kind = Space;
    std::string text;
};

bool id_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool id_char(char c) { return id_start(c) || (c >= '0' && c <= '9'); }
bool digit(char c) { return c >= '0' && c <= '9'; }

std::vector<Token> lex(const std::string& source) {
    std::vector<Token> tokens;
    size_t at = 0;
    while (at < source.size()) {
        char c = source[at];
        if (c == '/' && at + 1 < source.size() && source[at + 1] == '/') {
            while (at < source.size() && source[at] != '\n') ++at;
            continue;
        }
        if (c == '/' && at + 1 < source.size() && source[at + 1] == '*') {
            at += 2;
            while (at + 1 < source.size() && !(source[at] == '*' && source[at + 1] == '/')) ++at;
            at = at + 2 > source.size() ? source.size() : at + 2;
            continue;
        }
        Token token;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            token.kind = Token::Space;
            while (at < source.size() &&
                   (source[at] == ' ' || source[at] == '\t' || source[at] == '\r' || source[at] == '\n'))
                token.text += source[at++];
        } else if (id_start(c)) {
            token.kind = Token::Id;
            while (at < source.size() && id_char(source[at])) token.text += source[at++];
        } else if (digit(c) || (c == '.' && at + 1 < source.size() && digit(source[at + 1]))) {
            token.kind = Token::Number;
            while (at < source.size() && (id_char(source[at]) || source[at] == '.')) token.text += source[at++];
        } else {
            token.kind = Token::Punct;
            token.text += source[at++];
        }
        tokens.push_back(token);
    }
    return tokens;
}

/* One HLSL type per GLSL type, how many constant registers it takes, and
   whether the shader reads it as an integer rather than a float. */
struct TypeInfo {
    const char* hlsl;
    int registers;
    bool integer = false;
};

const std::unordered_map<std::string, TypeInfo>& type_table() {
    static const std::unordered_map<std::string, TypeInfo> table = {
        {"float", {"float", 1}},     {"vec2", {"float2", 1}},     {"vec3", {"float3", 1}},
        {"vec4", {"float4", 1}},     {"int", {"int", 1, true}},         {"ivec2", {"int2", 1, true}},
        {"ivec3", {"int3", 1, true}},      {"ivec4", {"int4", 1, true}},      {"uint", {"uint", 1, true}},
        {"uvec2", {"uint2", 1, true}},     {"uvec3", {"uint3", 1, true}},     {"uvec4", {"uint4", 1, true}},
        {"bool", {"bool", 1, true}},       {"bvec2", {"bool2", 1, true}},     {"bvec3", {"bool3", 1, true}},
        {"bvec4", {"bool4", 1, true}},     {"mat2", {"float2x2", 2}},   {"mat3", {"float3x3", 3}},
        {"mat4", {"float4x4", 4}},   {"mat3x3", {"float3x3", 3}}, {"mat4x4", {"float4x4", 4}},
    };
    return table;
}

const char* hlsl_type(const std::string& glsl) {
    auto found = type_table().find(glsl);
    return found == type_table().end() ? nullptr : found->second.hlsl;
}

bool is_qualifier(const std::string& word) {
    return word == "highp" || word == "mediump" || word == "lowp" || word == "flat" || word == "smooth" ||
           word == "centroid" || word == "invariant" || word == "precise";
}

/* A global declaration this translator lifted out of the source. */
struct Decl {
    std::string type;
    std::string name;
    int location = -1; /* from layout(location = N), when the shader said one */
};

struct Stage {
    std::vector<Token> body;
    std::vector<Decl> uniforms;
    std::vector<Decl> varyings; /* the vertex stage writes these, the fragment stage reads them */
    std::vector<Decl> outputs;  /* fragment colour outputs */
    std::vector<Decl> inputs;   /* vertex attributes, which have no buffer behind them */
    std::vector<Decl> samplers; /* a sampler2D takes a texture slot, not a constant register */
};

/* Pulls the global declarations out and leaves the code behind. */
bool split(const std::string& source, bool fragment, Stage& stage, std::string& error) {
    std::vector<Token> tokens = lex(source);
    int braces = 0;
    for (size_t at = 0; at < tokens.size(); ++at) {
        const Token& token = tokens[at];
        if (token.kind == Token::Punct) {
            if (token.text == "{") ++braces;
            if (token.text == "}") --braces;
        }
        bool global = braces == 0;
        if (global && token.kind == Token::Punct && token.text == "#") {
            while (at < tokens.size() && tokens[at].text.find('\n') == std::string::npos) ++at;
            continue;
        }
        if (global && token.kind == Token::Id && token.text == "precision") {
            while (at < tokens.size() && tokens[at].text != ";") ++at;
            continue;
        }
        if (global && token.kind == Token::Id &&
            (token.text == "uniform" || token.text == "in" || token.text == "out" || token.text == "layout" ||
             token.text == "attribute" || token.text == "varying")) {
            std::string keyword = token.text;
            int location = -1;
            if (keyword == "layout") {
                int depth = 0;
                bool at_location = false;
                while (at < tokens.size()) {
                    if (tokens[at].text == "(") ++depth;
                    if (tokens[at].kind == Token::Id && tokens[at].text == "location") at_location = true;
                    if (at_location && tokens[at].kind == Token::Number) {
                        location = std::atoi(tokens[at].text.c_str());
                        at_location = false;
                    }
                    if (tokens[at].text == ")") {
                        --depth;
                        if (depth == 0) break;
                    }
                    ++at;
                }
                ++at;
                while (at < tokens.size() && tokens[at].kind == Token::Space) ++at;
                if (at >= tokens.size()) break;
                keyword = tokens[at].text;
            }
            std::vector<std::string> words;
            while (++at < tokens.size() && tokens[at].text != ";")
                if (tokens[at].kind == Token::Id && !is_qualifier(tokens[at].text)) words.push_back(tokens[at].text);
            if (words.size() < 2) {
                error = "declaration without a type and a name: " + keyword;
                return false;
            }
            Decl decl{words[0], words[1], location};
            if (keyword == "uniform" && decl.type == "sampler2D") {
                stage.samplers.push_back(decl);
                continue;
            }
            if (keyword == "uniform" && decl.type.compare(0, 7, "sampler") == 0) {
                error = "only sampler2D is wired through, not " + decl.type;
                return false;
            }
            if (!hlsl_type(decl.type)) {
                error = "unsupported type in a " + keyword + " declaration: " + decl.type;
                return false;
            }
            if (keyword == "uniform") stage.uniforms.push_back(decl);
            else if (keyword == "out" && fragment) stage.outputs.push_back(decl);
            else if (keyword == "in" && !fragment) stage.inputs.push_back(decl);
            else stage.varyings.push_back(decl);
            continue;
        }
        stage.body.push_back(token);
    }
    return true;
}

/* Does this call have a comma between its own parentheses? */
bool two_args(const std::vector<Token>& tokens, size_t name_at) {
    size_t at = name_at + 1;
    while (at < tokens.size() && tokens[at].kind == Token::Space) ++at;
    if (at >= tokens.size() || tokens[at].text != "(") return false;
    int depth = 0;
    for (; at < tokens.size(); ++at) {
        if (tokens[at].text == "(") ++depth;
        else if (tokens[at].text == ")") {
            if (--depth == 0) return false;
        } else if (tokens[at].text == "," && depth == 1)
            return true;
    }
    return false;
}

const char* renamed_call(const std::string& name) {
    if (name == "mix") return "lerp";
    if (name == "fract") return "frac";
    if (name == "inversesqrt") return "rsqrt";
    if (name == "dFdx") return "ddx";
    if (name == "dFdy") return "ddy";
    if (name == "mod") return "qb_mod";
    if (name == "main") return "qb_glsl_main";
    return nullptr;
}

const char* rejected_call(const std::string& name) {
    if (name == "textureProj" || name == "textureGrad" || name == "textureOffset")
        return "this sampling form is not wired through yet";
    if (name == "gl_PointSize") return "point size has nothing behind it here";
    return nullptr;
}

/* HLSL samples through the texture object, so the sampler has to be named at
   the call. These take the guest's call apart and put it back that way. */
const char* sampling_call(const std::string& name) {
    if (name == "texture" || name == "texture2D") return "qb_texture";
    if (name == "textureLod") return "qb_texture_lod";
    if (name == "texelFetch") return "qb_texel_fetch";
    if (name == "textureSize") return "qb_texture_size";
    return nullptr;
}

bool emit_body(const Stage& stage, const std::vector<Decl>& samplers, std::string& out, std::string& error) {
    auto is_sampler = [&](const std::string& name) {
        for (const Decl& decl : samplers)
            if (decl.name == name) return true;
        return false;
    };
    for (size_t at = 0; at < stage.body.size(); ++at) {
        const Token& token = stage.body[at];
        if (token.kind != Token::Id) {
            out += token.text;
            continue;
        }
        if (const char* why = rejected_call(token.text)) {
            error = token.text + ": " + why;
            return false;
        }
        if (const char* sampling = sampling_call(token.text)) {
            size_t open_at = at + 1;
            while (open_at < stage.body.size() && stage.body[open_at].kind == Token::Space) ++open_at;
            size_t name_at = open_at + 1;
            while (name_at < stage.body.size() && stage.body[name_at].kind == Token::Space) ++name_at;
            if (open_at >= stage.body.size() || stage.body[open_at].text != "(" ||
                name_at >= stage.body.size() || !is_sampler(stage.body[name_at].text)) {
                error = token.text + " is called on something that is not a sampler2D uniform";
                return false;
            }
            const std::string& sampler = stage.body[name_at].text;
            out += std::string(sampling) + "(" + sampler + ", " + sampler + "_qb_state";
            at = name_at;
            continue;
        }
        if (const char* type = hlsl_type(token.text)) {
            out += type;
            continue;
        }
        if (token.text == "atan" && two_args(stage.body, at)) {
            out += "atan2";
            continue;
        }
        if (const char* call = renamed_call(token.text)) {
            out += call;
            continue;
        }
        out += token.text;
    }
    return true;
}

/* GLSL mod keeps the sign of the divisor. HLSL fmod does not, so it cannot stand in. */
const char* kHelpers =
    "\nfloat  qb_mod(float  x, float  y) { return x - y * floor(x / y); }\n"
    "float2 qb_mod(float2 x, float2 y) { return x - y * floor(x / y); }\n"
    "float2 qb_mod(float2 x, float  y) { return x - y * floor(x / y); }\n"
    "float3 qb_mod(float3 x, float3 y) { return x - y * floor(x / y); }\n"
    "float3 qb_mod(float3 x, float  y) { return x - y * floor(x / y); }\n"
    "float4 qb_mod(float4 x, float4 y) { return x - y * floor(x / y); }\n"
    "float4 qb_mod(float4 x, float  y) { return x - y * floor(x / y); }\n";

/* GLSL samples through the sampler; HLSL through the texture object. */
const char* kSampling =
    "float4 qb_texture(Texture2D t, SamplerState s, float2 uv) { return t.Sample(s, uv); }\n"
    "float4 qb_texture_lod(Texture2D t, SamplerState s, float2 uv, float lod)"
    " { return t.SampleLevel(s, uv, lod); }\n"
    "float4 qb_texel_fetch(Texture2D t, SamplerState s, int2 at, int lod)"
    " { return t.Load(int3(at, lod)); }\n"
    "int2 qb_texture_size(Texture2D t, SamplerState s, int lod) {\n"
    "    uint w = 0, h = 0, levels = 0;\n"
    "    t.GetDimensions((uint)lod, w, h, levels);\n"
    "    return int2(w, h);\n"
    "}\n"
    "int2 qb_texture_size(Texture2D t, SamplerState s) { return qb_texture_size(t, s, 0); }\n";

std::string varying_semantic(int index) { return "TEXCOORD" + std::to_string(index); }

}  // namespace

bool glsl_translate(const std::string& vertex_source, const std::string& fragment_source, GlslProgram& out) {
    Stage vertex;
    Stage fragment;
    if (!split(vertex_source, false, vertex, out.error)) {
        out.error = "vertex shader: " + out.error;
        return false;
    }
    if (!split(fragment_source, true, fragment, out.error)) {
        out.error = "fragment shader: " + out.error;
        return false;
    }
    /* Each vertex attribute gets a slot the driver can bind a buffer to. */
    std::string stage_in_vertex;
    for (size_t i = 0; i < vertex.inputs.size(); ++i) {
        const Decl& decl = vertex.inputs[i];
        GlslAttribute attribute;
        attribute.name = decl.name;
        attribute.location = decl.location >= 0 ? decl.location : (int)i;
        const char* hlsl = hlsl_type(decl.type);
        attribute.components = std::strcmp(hlsl, "float") == 0    ? 1
                               : std::strcmp(hlsl, "float2") == 0 ? 2
                               : std::strcmp(hlsl, "float3") == 0 ? 3
                                                                  : 4;
        out.attributes.push_back(attribute);
        stage_in_vertex += std::string("    ") + hlsl + " " + decl.name + " : " +
                           varying_semantic(attribute.location) + ";\n";
    }
    if (fragment.outputs.size() != 1) {
        out.error = "the fragment shader needs exactly one out colour";
        return false;
    }

    /* One register map for both stages, so a uniform sits at the same place in each. */
    std::unordered_map<std::string, GlslUniform> by_name;
    auto take = [&](const std::vector<Decl>& decls) {
        for (const Decl& decl : decls) {
            if (by_name.count(decl.name)) continue;
            GlslUniform uniform;
            uniform.name = decl.name;
            uniform.reg = out.registers;
            uniform.registers = type_table().find(decl.type)->second.registers;
            uniform.integer = type_table().find(decl.type)->second.integer;
            out.registers += uniform.registers;
            by_name[decl.name] = uniform;
            out.uniforms.push_back(uniform);
        }
    };
    take(vertex.uniforms);
    take(fragment.uniforms);

    std::string textures;
    auto bind = [&](const std::vector<Decl>& decls) {
        for (const Decl& decl : decls) {
            bool seen = false;
            for (const GlslSampler& sampler : out.samplers) seen = seen || sampler.name == decl.name;
            if (seen) continue;
            GlslSampler sampler;
            sampler.name = decl.name;
            sampler.slot = (int)out.samplers.size();
            out.samplers.push_back(sampler);
            std::string at = std::to_string(sampler.slot);
            textures += "Texture2D " + decl.name + " : register(t" + at + ");\n";
            textures += "SamplerState " + decl.name + "_qb_state : register(s" + at + ");\n";
        }
    };
    bind(vertex.samplers);
    bind(fragment.samplers);

    std::string block;
    std::vector<std::string> written;
    auto declare = [&](const std::vector<Decl>& decls) {
        for (const Decl& decl : decls) {
            bool seen = false;
            for (const std::string& name : written) seen = seen || name == decl.name;
            if (seen) continue;
            written.push_back(decl.name);
            block += std::string("    ") + hlsl_type(decl.type) + " " + decl.name + " : packoffset(c" +
                     std::to_string(by_name[decl.name].reg) + ");\n";
        }
    };
    declare(vertex.uniforms);
    declare(fragment.uniforms);
    std::string uniforms = "cbuffer QbUniforms : register(b0) {\n" + block + "};\n";
    uniforms += "cbuffer QbTarget : register(b1) { float4 qb_target : packoffset(c0); };\n";
    uniforms += textures;
    if (!out.samplers.empty()) uniforms += kSampling;

    std::string stage_out;
    for (size_t i = 0; i < vertex.varyings.size(); ++i)
        stage_out += std::string("    ") + hlsl_type(vertex.varyings[i].type) + " " + vertex.varyings[i].name + " : " +
                     varying_semantic((int)i) + ";\n";

    out.vertex = uniforms;
    out.vertex += kHelpers;
    out.vertex += "static uint gl_VertexID;\nstatic uint gl_InstanceID;\nstatic float4 gl_Position;\n";
    for (const Decl& decl : vertex.varyings)
        out.vertex += std::string("static ") + hlsl_type(decl.type) + " " + decl.name + ";\n";
    for (const Decl& decl : vertex.inputs)
        out.vertex += std::string("static ") + hlsl_type(decl.type) + " " + decl.name + ";\n";
    if (!emit_body(vertex, vertex.samplers, out.vertex, out.error)) {
        out.error = "vertex shader: " + out.error;
        return false;
    }
    out.vertex += "\nstruct QbVsOut { float4 qb_position : SV_Position;\n" + stage_out + "};\n";
    out.vertex += "struct QbVsIn {\n" + stage_in_vertex + "};\n";
    out.vertex +=
        "QbVsOut qb_vs(QbVsIn qb_in, uint qb_vertex : SV_VertexID, uint qb_instance : SV_InstanceID) {\n"
        "    gl_VertexID = qb_vertex;\n    gl_InstanceID = qb_instance;\n";
    for (const Decl& decl : vertex.inputs) out.vertex += "    " + decl.name + " = qb_in." + decl.name + ";\n";
    out.vertex +=
        "    qb_glsl_main();\n"
        "    QbVsOut qb_result;\n"
        /* GL clip space runs z from -w and D3D from 0, so the depth is remapped. */
        "    qb_result.qb_position = float4(gl_Position.xy, (gl_Position.z + gl_Position.w) * 0.5,"
        " gl_Position.w);\n";
    for (const Decl& decl : vertex.varyings)
        out.vertex += "    qb_result." + decl.name + " = " + decl.name + ";\n";
    out.vertex += "    return qb_result;\n}\n";

    /* The fragment stage reads the same varyings the vertex stage wrote, in the same order. */
    std::string stage_in;
    for (size_t i = 0; i < fragment.varyings.size(); ++i)
        stage_in += std::string("    ") + hlsl_type(fragment.varyings[i].type) + " " + fragment.varyings[i].name +
                    " : " + varying_semantic((int)i) + ";\n";

    out.fragment = uniforms;
    out.fragment += kHelpers;
    out.fragment += "static float4 gl_FragCoord;\nstatic bool gl_FrontFacing;\nstatic float gl_FragDepth;\n";
    for (const Decl& decl : fragment.varyings)
        out.fragment += std::string("static ") + hlsl_type(decl.type) + " " + decl.name + ";\n";
    for (const Decl& decl : fragment.outputs)
        out.fragment += std::string("static ") + hlsl_type(decl.type) + " " + decl.name + ";\n";
    if (!emit_body(fragment, fragment.samplers, out.fragment, out.error)) {
        out.error = "fragment shader: " + out.error;
        return false;
    }
    out.fragment += "\nstruct QbPsIn { float4 qb_position : SV_Position;\n" + stage_in + "};\n";
    out.fragment +=
        "float4 qb_ps(QbPsIn qb_in, bool qb_front : SV_IsFrontFace) : SV_Target {\n"
        /* GLSL counts gl_FragCoord.y from the bottom of the target. D3D counts from the top. */
        "    gl_FragCoord = float4(qb_in.qb_position.x, qb_target.y - qb_in.qb_position.y, qb_in.qb_position.z,"
        " qb_in.qb_position.w);\n"
        "    gl_FrontFacing = qb_front;\n";
    for (const Decl& decl : fragment.varyings) out.fragment += "    " + decl.name + " = qb_in." + decl.name + ";\n";
    out.fragment += "    qb_glsl_main();\n    return " + fragment.outputs[0].name + ";\n}\n";
    return true;
}
