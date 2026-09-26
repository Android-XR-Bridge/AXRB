/* Contract between the Quest-side OpenXR runtime and the Windows host.
   A frame is a shared GPU image the host already imported. This header has
   no pixel pointer and no readback message. */

#ifndef QUEST_BRIDGE_PROTOCOL_H
#define QUEST_BRIDGE_PROTOCOL_H

#include <stdint.h>

#define QB_MAGIC 0x31304251u /* 'QB01' */
#define QB_VERSION 1u

/* Local stream. One WriteFile is one message: uint32_t QbMsg, then the struct. */
#define QB_PIPE_NAME "\\\\.\\pipe\\quest-bridge"

typedef enum QbMsg {
    QB_MSG_HELLO = 1,  /* runtime -> host, once */
    QB_MSG_VIEWS = 2,  /* host -> runtime, when the session is up */
    QB_MSG_POSE = 3,   /* host -> runtime, every frame */
    QB_MSG_HAPTIC = 4, /* runtime -> host */
    QB_MSG_FRAME = 5,  /* runtime -> host: the shared eye images are ready */
    QB_MSG_SHARE = 6   /* host -> runtime: NT handles of the shared eye textures */
} QbMsg;

typedef struct QbXform {
    float px, py, pz;
    float qx, qy, qz, qw;
} QbXform;

typedef struct QbHello {
    uint32_t magic;
    uint32_t version;
} QbHello;

typedef struct QbViews {
    uint32_t view_count;
    uint32_t width;
    uint32_t height;
    uint32_t format; /* DXGI format of the shared images */
    float fov_left[2];
    float fov_right[2];
    float fov_up[2];
    float fov_down[2];
} QbViews;

typedef struct QbPose {
    int64_t display_time_ns;
    QbXform head;
    QbXform view[2]; /* per-eye pose in the same space as head */
    QbXform hand[2]; /* 0 left, 1 right */
    uint32_t hand_active;
} QbPose;

/* Handles are NT shared handles duplicated into the receiver process.
   Each eye texture has its own keyed mutex. Lock eye 0, then eye 1.
   Key 0 is the producer, key 1 is the host. */
typedef struct QbShare {
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t _pad;
    uint64_t handle[2];
} QbShare;

typedef struct QbHaptic {
    uint32_t hand;
    float amplitude;
    float duration_s;
    float frequency_hz;
} QbHaptic;

/* The pose the client actually rendered this image for. The host submits it
   rather than its own newer pose, so the compositor can warp the image to
   where the head is now. Without it the picture swims when you turn. */
typedef struct QbFrame {
    uint32_t image_index;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    QbXform view[2];
    float fov[2][4]; /* left, right, up, down */
} QbFrame;

#endif
