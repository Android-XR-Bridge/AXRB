#include "link.h"

#include "wire.h"

#include <cstdio>

namespace {

bool fail_hr(const char* what, HRESULT hr) {
    std::fprintf(stderr, "qb-host: %s failed (0x%08lx)\n", what, (unsigned long)hr);
    return false;
}

DWORD WINAPI link_thread(void* param) {
    auto* link = static_cast<Link*>(param);
    while (link->running) {
        BOOL connected = ConnectNamedPipe(link->pipe, nullptr);
        if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
            if (!link->running) break;
            continue;
        }
        ULONG pid = 0;
        GetNamedPipeClientProcessId(link->pipe, &pid);
        HANDLE process = OpenProcess(PROCESS_DUP_HANDLE, FALSE, pid);
        QbShare share{};
        share.width = link->views.width;
        share.height = link->views.height;
        share.format = link->views.format;
        bool handed = process != nullptr;
        for (int i = 0; i < 2 && handed; ++i) {
            HANDLE remote = nullptr;
            handed = DuplicateHandle(GetCurrentProcess(), link->nt[i], process, &remote, 0, FALSE,
                                     DUPLICATE_SAME_ACCESS) != 0;
            share.handle[i] = (uint64_t)(uintptr_t)remote;
        }
        if (process) CloseHandle(process);
        if (!handed || !qb_write(link->pipe, QB_MSG_SHARE, &share, sizeof(share))) {
            DisconnectNamedPipe(link->pipe);
            continue;
        }
        link->views_sent = 0;
        link->sent_seq = 0;
        link->client = 1;
        DWORD mode = PIPE_READMODE_MESSAGE | PIPE_NOWAIT;
        SetNamedPipeHandleState(link->pipe, &mode, nullptr, nullptr);
        std::printf("qb-host: client %lu connected\n", pid);
        std::fflush(stdout);

        while (link->running) {
            DWORD avail = 0;
            if (!PeekNamedPipe(link->pipe, nullptr, 0, nullptr, &avail, nullptr)) break;
            if (avail > 0) {
                QbMsgBuf msg{};
                if (!qb_read(link->pipe, &msg)) {
                    if (GetLastError() == ERROR_NO_DATA) continue;
                    break;
                }
                if (msg.type == QB_MSG_FRAME) {
                    /* Keep the pose this picture was drawn for. */
                    EnterCriticalSection(&link->cs);
                    std::memcpy(&link->frame, msg.body, sizeof(QbFrame));
                    LeaveCriticalSection(&link->cs);
                    InterlockedIncrement(&link->frames);
                }
                continue;
            }
            long seq = link->pose_seq;
            QbPose pose{};
            QbViews views{};
            EnterCriticalSection(&link->cs);
            pose = link->pose;
            views = link->views;
            LeaveCriticalSection(&link->cs);
            if (!link->views_sent && views.width != 0 && seq != 0) {
                if (!qb_write(link->pipe, QB_MSG_VIEWS, &views, sizeof(views))) {
                    DWORD err = GetLastError();
                    if (err == ERROR_NO_DATA || err == ERROR_PIPE_BUSY) {
                        Sleep(1);
                        continue;
                    }
                    break;
                }
                link->views_sent = 1;
            }
            if (seq != link->sent_seq) {
                if (!qb_write(link->pipe, QB_MSG_POSE, &pose, sizeof(pose))) {
                    DWORD err = GetLastError();
                    if (err == ERROR_NO_DATA || err == ERROR_PIPE_BUSY) {
                        Sleep(1);
                        continue;
                    }
                    break;
                }
                link->sent_seq = seq;
            } else {
                Sleep(1);
            }
        }
        link->client = 0;
        /* Repeating the last frame is for a frame that was missed. Once the
           client is gone there is nothing coming, so go back to standby rather
           than leaving its last picture frozen in the headset. */
        link->have_held = false;
        link->frames = 0;
        std::printf("qb-host: client disconnected\n");
        std::fflush(stdout);
        DisconnectNamedPipe(link->pipe);
    }
    return 0;
}

}  // namespace

bool Link::start(ID3D11Device* device, uint32_t width, uint32_t height, DXGI_FORMAT format) {
    InitializeCriticalSection(&cs);
    cs_ready = true;
    views.width = width;
    views.height = height;
    views.format = (uint32_t)format;
    views.view_count = 2;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    for (int i = 0; i < 2; ++i) {
        HRESULT hr = device->CreateTexture2D(&desc, nullptr, &eye[i]);
        if (FAILED(hr)) {
            stop();
            return fail_hr("CreateTexture2D(shared)", hr);
        }
        IDXGIResource1* resource = nullptr;
        hr = eye[i]->QueryInterface(IID_PPV_ARGS(&resource));
        if (FAILED(hr)) {
            stop();
            return fail_hr("QueryInterface(IDXGIResource1)", hr);
        }
        hr = resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                           nullptr, &nt[i]);
        resource->Release();
        if (FAILED(hr)) {
            stop();
            return fail_hr("CreateSharedHandle", hr);
        }
        desc.MiscFlags = 0;
        hr = device->CreateTexture2D(&desc, nullptr, &held[i]);
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
        if (FAILED(hr)) {
            stop();
            return fail_hr("CreateTexture2D(held)", hr);
        }
        hr = eye[i]->QueryInterface(IID_PPV_ARGS(&mutex[i]));
        if (FAILED(hr)) {
            stop();
            return fail_hr("QueryInterface(IDXGIKeyedMutex)", hr);
        }
    }

    pipe = CreateNamedPipeA(QB_PIPE_NAME, PIPE_ACCESS_DUPLEX,
                            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, 64 * 1024, 64 * 1024, 0,
                            nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "qb-host: CreateNamedPipe failed (%lu)\n", GetLastError());
        stop();
        return false;
    }
    running = 1;
    thread = CreateThread(nullptr, 0, link_thread, this, 0, nullptr);
    if (!thread) {
        std::fprintf(stderr, "qb-host: CreateThread failed (%lu)\n", GetLastError());
        stop();
        return false;
    }
    std::printf("qb-host: pipe %s\n", QB_PIPE_NAME);
    std::fflush(stdout);
    return true;
}

void Link::stop() {
    running = 0;
    if (thread) {
        CancelSynchronousIo(thread);
        WaitForSingleObject(thread, 2000);
        CloseHandle(thread);
        thread = nullptr;
    }
    if (pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe);
        pipe = INVALID_HANDLE_VALUE;
    }
    for (int i = 0; i < 2; ++i) {
        if (mutex[i]) {
            mutex[i]->Release();
            mutex[i] = nullptr;
        }
        if (nt[i]) {
            CloseHandle(nt[i]);
            nt[i] = nullptr;
        }
        if (held[i]) {
            held[i]->Release();
            held[i] = nullptr;
        }
        if (eye[i]) {
            eye[i]->Release();
            eye[i] = nullptr;
        }
    }
    have_held = false;
    if (cs_ready) {
        DeleteCriticalSection(&cs);
        cs_ready = false;
    }
}

void Link::publish(const QbPose& next, const QbViews& next_views) {
    EnterCriticalSection(&cs);
    pose = next;
    views.fov_left[0] = next_views.fov_left[0];
    views.fov_left[1] = next_views.fov_left[1];
    views.fov_right[0] = next_views.fov_right[0];
    views.fov_right[1] = next_views.fov_right[1];
    views.fov_up[0] = next_views.fov_up[0];
    views.fov_up[1] = next_views.fov_up[1];
    views.fov_down[0] = next_views.fov_down[0];
    views.fov_down[1] = next_views.fov_down[1];
    LeaveCriticalSection(&cs);
    InterlockedIncrement(&pose_seq);
}

bool Link::begin_copy(ID3D11DeviceContext*) {
    copying = false;
    if (!frames || mutex[0] == nullptr || mutex[1] == nullptr) return false;
    /* Never wait. The headset frame is due every 11ms and the client holds
       this lock for a whole frame of its own, so waiting here costs the
       display deadline. A frame that is not ready simply arrives next time. */
    if (mutex[0]->AcquireSync(1, 0) != S_OK) return false;
    if (mutex[1]->AcquireSync(1, 0) != S_OK) {
        mutex[0]->ReleaseSync(0);
        return false;
    }
    copying = true;
    return true;
}

QbFrame Link::last_frame() {
    EnterCriticalSection(&cs);
    QbFrame copy = frame;
    LeaveCriticalSection(&cs);
    return copy;
}

void Link::copy_eye(ID3D11DeviceContext* context, int index, ID3D11Texture2D* dst) {
    context->CopyResource(dst, eye[index]);
    context->CopyResource(held[index], eye[index]);
}

bool Link::copy_held(ID3D11DeviceContext* context, int index, ID3D11Texture2D* dst) {
    if (!have_held) return false;
    context->CopyResource(dst, held[index]);
    return true;
}

void Link::end_copy(ID3D11DeviceContext* context) {
    if (!copying) return;
    context->Flush();
    mutex[1]->ReleaseSync(0);
    mutex[0]->ReleaseSync(0);
    have_held = true;
    copying = false;
}
