/* Arm64 Android guest. OpenXR plus the OpenGL ES driver in qb-guest.
   memset comes from the stand-in libc. Nothing here draws with D3D, and the
   shaders below are the ones that get compiled: size is an ivec2 because this
   guest has no FP registers to convert a width into a float with. */

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <string.h>

#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

static const char* k_vs =
    "#version 300 es\n"
    "void main() {\n"
    "  vec2 c = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "  gl_Position = vec4(c * vec2(2.0, -2.0) + vec2(-1.0, 1.0), 0.0, 1.0);\n"
    "}\n";

static const char* k_fs =
    "#version 300 es\n"
    "precision highp float;\n"
    "uniform vec4 quat;\n"
    "uniform vec4 pos;\n"
    "uniform ivec2 size;\n"
    "uniform vec4 fov;\n"
    "uniform sampler2D tile;\n"
    "out vec4 color;\n"
    "vec3 qrot(vec4 q, vec3 v) { return v + 2.0 * cross(q.xyz, q.w * v + cross(q.xyz, v)); }\n"
    "void main() {\n"
    "  vec2 uv = gl_FragCoord.xy / vec2(size);\n"
    "  float x = mix(tan(fov.x), tan(fov.y), uv.x);\n"
    "  float y = mix(tan(fov.w), tan(fov.z), uv.y);\n"
    "  vec3 dir = qrot(quat, normalize(vec3(x, y, -1.0)));\n"
    "  if (dir.y < -0.02) {\n"
    "    float t = -1.6 / dir.y;\n"
    "    vec3 hit = pos.xyz + dir * t;\n"
    "    float gx = abs(fract(hit.x) - 0.5);\n"
    "    float gz = abs(fract(hit.z) - 0.5);\n"
    "    float grid = clamp(1.0 - min(gx, gz) * 30.0, 0.0, 1.0);\n"
    "    float checker = mod(floor(hit.x) + floor(hit.z), 2.0);\n"
    "    vec3 base = checker > 0.5 ? vec3(0.45, 0.22, 0.55) : vec3(0.10, 0.05, 0.16);\n"
    "    vec3 tinted = base * texture(tile, hit.xz * 0.5).rgb * 2.0;\n"
    "    color = vec4(mix(tinted, vec3(0.95, 0.9, 1.0), grid), 1.0);\n"
    "  } else {\n"
    "    color = vec4(mix(vec3(0.95, 0.75, 0.35), vec3(0.05, 0.02, 0.08), clamp(dir.y * 1.4, 0.0, 1.0)), 1.0);\n"
    "  }\n"
    "}\n";

/* A second program, drawn from a vertex buffer instead of from gl_VertexID.
   The projection is built in the shader from the same pose uniforms, because
   the guest computes the offset in real floating point. */
static const char* k_mesh_vs =
    "#version 300 es\n"
    "uniform vec4 quat;\n"
    "uniform vec4 pos;\n"
    "uniform vec4 fov;\n"
    "uniform vec4 offset;\n"
    "layout(location = 0) in vec3 in_position;\n"
    "layout(location = 1) in vec3 in_colour;\n"
    "out vec3 v_colour;\n"
    "vec3 qrot(vec4 q, vec3 v) { return v + 2.0 * cross(q.xyz, q.w * v + cross(q.xyz, v)); }\n"
    "void main() {\n"
    "  vec3 rel = in_position + offset.xyz - pos.xyz;\n"
    "  vec3 view = qrot(vec4(-quat.xyz, quat.w), rel);\n"
    "  float l = tan(fov.x), r = tan(fov.y), u = tan(fov.z), d = tan(fov.w);\n"
    "  float zn = 0.05, zf = 200.0;\n"
    "  gl_Position = vec4((2.0 * view.x + view.z * (r + l)) / (r - l),\n"
    "                     (2.0 * view.y + view.z * (u + d)) / (u - d),\n"
    "                     -view.z * (zf + zn) / (zf - zn) - 2.0 * zf * zn / (zf - zn),\n"
    "                     -view.z);\n"
    "  v_colour = in_colour;\n"
    "}\n";

static const char* k_mesh_fs =
    "#version 300 es\n"
    "precision highp float;\n"
    "in vec3 v_colour;\n"
    "out vec4 color;\n"
    "void main() {\n"
    "  color = vec4(v_colour, 1.0);\n"
    "}\n";

/* Position then colour, interleaved, so both attributes read one buffer.
   These are constants in the binary: the guest never computes a float. */
static const float g_cube[] = {
    -0.35f, -0.35f, -1.65f, 0.95f, 0.25f, 0.25f, 0.35f,  -0.35f, -1.65f, 0.95f, 0.85f, 0.25f,
    0.35f,  0.35f,  -1.65f, 0.25f, 0.95f, 0.35f, -0.35f, 0.35f,  -1.65f, 0.25f, 0.85f, 0.95f,
    -0.35f, -0.35f, -2.35f, 0.85f, 0.35f, 0.95f, 0.35f,  -0.35f, -2.35f, 0.95f, 0.55f, 0.15f,
    0.35f,  0.35f,  -2.35f, 0.35f, 0.95f, 0.85f, -0.35f, 0.35f,  -2.35f, 0.95f, 0.95f, 0.95f,
};

/* Counter-clockwise seen from outside, which is the front face in GL. */
static const unsigned short g_index[] = {
    0, 1, 2, 0, 2, 3, /* near */
    5, 4, 7, 5, 7, 6, /* far */
    4, 0, 3, 4, 3, 7, /* left */
    1, 5, 6, 1, 6, 2, /* right */
    3, 2, 6, 3, 6, 7, /* top */
    4, 5, 1, 4, 1, 0, /* bottom */
};

/* Real floating point in the guest: no libm here, so this is a parabola fit
   to a sine over -pi..pi. It is the arithmetic that proves the interpreter
   is doing FP rather than the compiler hiding it in integer registers. */
static float guest_sin(float x) {
    float y = 1.27323954f * x - 0.405284735f * x * (x < 0.f ? -x : x);
    return 0.225f * (y * (y < 0.f ? -y : y) - y) + y;
}

static float g_phase;

static unsigned g_mesh_prog;
static unsigned g_mesh_vbo;
static unsigned g_mesh_ibo;
static int g_mesh_quat;
static int g_mesh_pos;
static int g_mesh_fov;
static int g_mesh_offset;

static unsigned g_prog;
static int g_quat;
static int g_pos;
static int g_fov;
static int g_size;
static int g_tile;
static unsigned g_texture;

/* Four quadrants, built with integer arithmetic because this guest has no
   floating point registers. In GL the first row is the bottom of the picture,
   so rows 0..31 are the bottom half: red on the left, green on the right. */
static unsigned char g_pixels[64 * 64 * 4];

static void make_texture(void) {
    int y;
    int x;
    for (y = 0; y < 64; ++y) {
        for (x = 0; x < 64; ++x) {
            unsigned char* p = &g_pixels[(y * 64 + x) * 4];
            int quadrant = (x < 32 ? 0 : 1) + (y < 32 ? 0 : 2);
            p[0] = quadrant == 0 ? 220 : quadrant == 1 ? 40 : quadrant == 2 ? 40 : 220;
            p[1] = quadrant == 0 ? 40 : quadrant == 1 ? 220 : quadrant == 2 ? 40 : 220;
            p[2] = quadrant == 0 ? 40 : quadrant == 1 ? 40 : quadrant == 2 ? 220 : 40;
            p[3] = 255;
        }
    }
    glGenTextures(1, &g_texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

static void make_program(void) {
    unsigned vs = glCreateShader(GL_VERTEX_SHADER);
    unsigned fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(vs, 1, &k_vs, 0);
    glShaderSource(fs, 1, &k_fs, 0);
    glCompileShader(vs);
    glCompileShader(fs);
    g_prog = glCreateProgram();
    glAttachShader(g_prog, vs);
    glAttachShader(g_prog, fs);
    glLinkProgram(g_prog);
    g_quat = glGetUniformLocation(g_prog, "quat");
    g_pos = glGetUniformLocation(g_prog, "pos");
    g_fov = glGetUniformLocation(g_prog, "fov");
    g_size = glGetUniformLocation(g_prog, "size");
    g_tile = glGetUniformLocation(g_prog, "tile");
    make_texture();
}

static void make_mesh(void) {
    unsigned vs = glCreateShader(GL_VERTEX_SHADER);
    unsigned fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(vs, 1, &k_mesh_vs, 0);
    glShaderSource(fs, 1, &k_mesh_fs, 0);
    glCompileShader(vs);
    glCompileShader(fs);
    g_mesh_prog = glCreateProgram();
    glAttachShader(g_mesh_prog, vs);
    glAttachShader(g_mesh_prog, fs);
    glLinkProgram(g_mesh_prog);
    g_mesh_quat = glGetUniformLocation(g_mesh_prog, "quat");
    g_mesh_pos = glGetUniformLocation(g_mesh_prog, "pos");
    g_mesh_fov = glGetUniformLocation(g_mesh_prog, "fov");
    g_mesh_offset = glGetUniformLocation(g_mesh_prog, "offset");

    glGenBuffers(1, &g_mesh_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_mesh_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(g_cube), g_cube, GL_STATIC_DRAW);
    glGenBuffers(1, &g_mesh_ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_mesh_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(g_index), g_index, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (const void*)0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (const void*)12);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
}

static void draw_mesh(float qx, float qy, float qz, float qw, float px, float py, float pz, float left, float right,
                      float up, float down) {
    /* Only the cube is wound for culling. The floor triangle is not. */
    glEnable(GL_CULL_FACE);
    glUseProgram(g_mesh_prog);
    glUniform4f(g_mesh_quat, qx, qy, qz, qw);
    glUniform4f(g_mesh_pos, px, py, pz, 0.f);
    glUniform4f(g_mesh_fov, left, right, up, down);
    glUniform4f(g_mesh_offset, 0.6f * guest_sin(g_phase), 0.35f * guest_sin(g_phase * 2.f), 0.f, 0.f);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, (const void*)0);
    glDisable(GL_CULL_FACE);
}

static void draw_eye(unsigned tex, int width, int height, float qx, float qy, float qz, float qw, float px, float py,
                     float pz, float left, float right, float up, float down) {
    unsigned fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glViewport(0, 0, width, height);
    glUseProgram(g_prog);
    glUniform4f(g_quat, qx, qy, qz, qw);
    glUniform4f(g_pos, px, py, pz, 0.f);
    glUniform4f(g_fov, left, right, up, down);
    glUniform2i(g_size, width, height);
    glUniform1i(g_tile, 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    draw_mesh(qx, qy, qz, qw, px, py, pz, left, right, up, down);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
}

void qb_guest_main(void) {
    XrInstance instance = 0;
    XrInstanceCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.type = XR_TYPE_INSTANCE_CREATE_INFO;
    info.applicationInfo.applicationVersion = 1;
    info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    const char* ext = "XR_KHR_opengl_es_enable";
    info.enabledExtensionCount = 1;
    info.enabledExtensionNames = &ext;
    if (xrCreateInstance(&info, &instance) != XR_SUCCESS) return;

    XrSystemGetInfo system_info;
    memset(&system_info, 0, sizeof(system_info));
    system_info.type = XR_TYPE_SYSTEM_GET_INFO;
    system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system = 0;
    if (xrGetSystem(instance, &system_info, &system) != XR_SUCCESS) return;

    XrSessionCreateInfo session_info;
    memset(&session_info, 0, sizeof(session_info));
    session_info.type = XR_TYPE_SESSION_CREATE_INFO;
    session_info.systemId = system;
    XrSession session = 0;
    if (xrCreateSession(instance, &session_info, &session) != XR_SUCCESS) return;

    XrViewConfigurationView configs[2];
    memset(configs, 0, sizeof(configs));
    configs[0].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    configs[1].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    uint32_t view_count = 0;
    xrEnumerateViewConfigurationViews(instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &view_count,
                                      configs);

    int64_t format = 0;
    uint32_t format_count = 0;
    xrEnumerateSwapchainFormats(session, 1, &format_count, &format);

    XrSwapchain swap[2];
    memset(swap, 0, sizeof(swap));
    for (uint32_t eye = 0; eye < 2; ++eye) {
        XrSwapchainCreateInfo swap_info;
        memset(&swap_info, 0, sizeof(swap_info));
        swap_info.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
        swap_info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        swap_info.format = format;
        swap_info.sampleCount = 1;
        swap_info.width = configs[eye].recommendedImageRectWidth;
        swap_info.height = configs[eye].recommendedImageRectHeight;
        swap_info.faceCount = 1;
        swap_info.arraySize = 1;
        swap_info.mipCount = 1;
        xrCreateSwapchain(session, &swap_info, &swap[eye]);
    }
    make_program();
    make_mesh();

    int running = 0;
    int presented = 0;
    XrSessionState state = XR_SESSION_STATE_UNKNOWN;
    while (state != XR_SESSION_STATE_EXITING) {
        XrEventDataBuffer event;
        memset(&event, 0, sizeof(event));
        event.type = XR_TYPE_EVENT_DATA_BUFFER;
        if (xrPollEvent(instance, &event) == XR_SUCCESS && event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            XrEventDataSessionStateChanged* changed = (XrEventDataSessionStateChanged*)&event;
            state = changed->state;
            if (state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo begin;
                memset(&begin, 0, sizeof(begin));
                begin.type = XR_TYPE_SESSION_BEGIN_INFO;
                begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                xrBeginSession(session, &begin);
                running = 1;
            } else if (state == XR_SESSION_STATE_STOPPING) {
                xrEndSession(session);
                running = 0;
            }
            continue;
        }
        if (!running) continue;

        XrFrameWaitInfo wait_info;
        XrFrameState frame;
        memset(&wait_info, 0, sizeof(wait_info));
        memset(&frame, 0, sizeof(frame));
        wait_info.type = XR_TYPE_FRAME_WAIT_INFO;
        frame.type = XR_TYPE_FRAME_STATE;
        if (xrWaitFrame(session, &wait_info, &frame) != XR_SUCCESS) break;

        XrFrameBeginInfo begin_info;
        memset(&begin_info, 0, sizeof(begin_info));
        begin_info.type = XR_TYPE_FRAME_BEGIN_INFO;
        xrBeginFrame(session, &begin_info);

        XrViewLocateInfo locate;
        XrViewState view_state;
        XrView views[2];
        memset(&locate, 0, sizeof(locate));
        memset(&view_state, 0, sizeof(view_state));
        memset(views, 0, sizeof(views));
        locate.type = XR_TYPE_VIEW_LOCATE_INFO;
        locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate.displayTime = frame.predictedDisplayTime;
        view_state.type = XR_TYPE_VIEW_STATE;
        views[0].type = XR_TYPE_VIEW;
        views[1].type = XR_TYPE_VIEW;
        uint32_t located = 0;
        xrLocateViews(session, &locate, &view_state, 2, &located, views);

        XrCompositionLayerProjectionView projection[2];
        memset(projection, 0, sizeof(projection));
        for (uint32_t eye = 0; eye < 2; ++eye) {
            uint32_t index = 0;
            xrAcquireSwapchainImage(swap[eye], 0, &index);
            XrSwapchainImageWaitInfo image_wait;
            memset(&image_wait, 0, sizeof(image_wait));
            image_wait.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
            image_wait.timeout = 0x7fffffffffffffffLL;
            xrWaitSwapchainImage(swap[eye], &image_wait);
            XrSwapchainImageOpenGLESKHR picture;
            memset(&picture, 0, sizeof(picture));
            picture.type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
            uint32_t picture_count = 0;
            xrEnumerateSwapchainImages(swap[eye], 1, &picture_count, (XrSwapchainImageBaseHeader*)&picture);
            draw_eye(picture.image, (int)configs[eye].recommendedImageRectWidth,
                     (int)configs[eye].recommendedImageRectHeight, views[eye].pose.orientation.x,
                     views[eye].pose.orientation.y, views[eye].pose.orientation.z, views[eye].pose.orientation.w,
                     views[eye].pose.position.x, views[eye].pose.position.y, views[eye].pose.position.z,
                     views[eye].fov.angleLeft, views[eye].fov.angleRight, views[eye].fov.angleUp,
                     views[eye].fov.angleDown);
            xrReleaseSwapchainImage(swap[eye], 0);
            projection[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            projection[eye].pose = views[eye].pose;
            projection[eye].fov = views[eye].fov;
            projection[eye].subImage.swapchain = swap[eye];
            projection[eye].subImage.imageRect.extent.width = (int32_t)configs[eye].recommendedImageRectWidth;
            projection[eye].subImage.imageRect.extent.height = (int32_t)configs[eye].recommendedImageRectHeight;
        }

        XrCompositionLayerProjection layer;
        memset(&layer, 0, sizeof(layer));
        layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
        layer.viewCount = 2;
        layer.views = projection;
        const XrCompositionLayerBaseHeader* layers[1];
        layers[0] = (const XrCompositionLayerBaseHeader*)&layer;
        XrFrameEndInfo end;
        memset(&end, 0, sizeof(end));
        end.type = XR_TYPE_FRAME_END_INFO;
        end.displayTime = frame.predictedDisplayTime;
        end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        end.layerCount = frame.shouldRender ? 1 : 0;
        end.layers = end.layerCount ? layers : 0;
        xrEndFrame(session, &end);
        if (frame.shouldRender) presented += 1;
        g_phase += 0.03f;
        if (g_phase > 3.14159265f) g_phase -= 6.2831853f;
    }
    xrDestroySession(session);
    xrDestroyInstance(instance);
}
