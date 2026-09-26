#pragma once

#include "bridge.h"

#include <d3d11.h>
#include <dxgi1_2.h>

#include <windows.h>

/* Serves the shared eye textures and the latest pose to one client. */
struct Link {
    bool start(ID3D11Device* device, uint32_t width, uint32_t height, DXGI_FORMAT format);
    void stop();
    void publish(const QbPose& pose, const QbViews& views);
    /* Acquires the producer mutex. False when no client frame is waiting. */
    bool begin_copy(ID3D11DeviceContext* context);
    void copy_eye(ID3D11DeviceContext* context, int eye, ID3D11Texture2D* dst);
    /* Previous good frame. Used when the producer still holds the mutex. */
    bool copy_held(ID3D11DeviceContext* context, int eye, ID3D11Texture2D* dst);
    /* The pose the client drew the frame it just handed over for. */
    QbFrame last_frame();
    void end_copy(ID3D11DeviceContext* context);

    CRITICAL_SECTION cs{};
    bool cs_ready = false;
    ID3D11Texture2D* eye[2] = {};
    ID3D11Texture2D* held[2] = {};
    IDXGIKeyedMutex* mutex[2] = {};
    bool have_held = false;
    HANDLE nt[2] = {nullptr, nullptr};
    HANDLE pipe = INVALID_HANDLE_VALUE;
    HANDLE thread = nullptr;
    volatile long running = 0;
    volatile long client = 0;
    volatile long frames = 0;
    QbFrame frame{};
    QbPose pose{};
    QbViews views{};
    volatile long pose_seq = 0;
    volatile long sent_seq = 0;
    volatile long views_sent = 0;
    bool copying = false;
};
