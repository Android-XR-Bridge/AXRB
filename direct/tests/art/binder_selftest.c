#include <errno.h>
#include <fcntl.h>
#include <linux/android/binder.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Binder check failed line %d: %s (errno %d)\n", __LINE__, #x, errno); _exit(1); } } while (0)
static int server;

static void send_command(int fd, uint32_t command, const void* payload) {
    unsigned char buffer[128];
    unsigned size = _IOC_SIZE(command);
    memcpy(buffer, &command, 4);
    if (size) memcpy(buffer + 4, payload, size);
    struct binder_write_read wr = {.write_size = 4 + size, .write_buffer = (uintptr_t)buffer};
    CHECK(ioctl(fd, BINDER_WRITE_READ, &wr) == 0);
    CHECK(wr.write_consumed == wr.write_size);
}

static struct binder_transaction_data receive_transaction(int fd, uint32_t expected) {
    for (;;) {
        unsigned char buffer[512];
        struct binder_write_read wr = {.read_size = sizeof buffer, .read_buffer = (uintptr_t)buffer};
        CHECK(ioctl(fd, BINDER_WRITE_READ, &wr) == 0);
        for (size_t at = 0; at < wr.read_consumed;) {
            uint32_t command;
            memcpy(&command, buffer + at, 4);
            unsigned size = _IOC_SIZE(command);
            CHECK(at + 4 + size <= wr.read_consumed);
            if (command == expected) {
                struct binder_transaction_data tr;
                memcpy(&tr, buffer + at + 4, sizeof tr);
                return tr;
            }
            CHECK(command == BR_TRANSACTION_COMPLETE || command == BR_NOOP);
            at += 4 + size;
        }
    }
}

static void* serve(void* ignored) {
    (void)ignored;
    send_command(server, BC_ENTER_LOOPER, 0);
    for (int i = 0; i < 100; ++i) {
        struct binder_transaction_data tr = receive_transaction(server, BR_TRANSACTION);
        CHECK(tr.target.ptr == 0x1234 && tr.cookie == 0x5678 && tr.code == 42);
        CHECK(tr.data_size == sizeof(int) && tr.offsets_size == 0);
        int value;
        memcpy(&value, (void*)(uintptr_t)tr.data.ptr.buffer, sizeof value);
        CHECK(value == i);
        send_command(server, BC_FREE_BUFFER, &tr.data.ptr.buffer);
        value *= 2;
        struct binder_transaction_data reply = {.data_size = sizeof value};
        reply.data.ptr.buffer = (uintptr_t)&value;
        send_command(server, BC_REPLY, &reply);
    }
    send_command(server, BC_EXIT_LOOPER, 0);
    return 0;
}

int main(void) {
    server = open("/dev/binder", O_RDWR | O_CLOEXEC);
    int client = open("/dev/binder", O_RDWR | O_CLOEXEC);
    CHECK(server >= 0 && client >= 0);
    struct binder_version version = {0};
    CHECK(ioctl(client, BINDER_VERSION, &version) == 0 && version.protocol_version == 8);
    CHECK(mmap(0, 1024 * 1024, PROT_READ, MAP_PRIVATE, server, 0) != MAP_FAILED);
    CHECK(mmap(0, 1024 * 1024, PROT_READ, MAP_PRIVATE, client, 0) != MAP_FAILED);
    struct flat_binder_object manager = {.hdr.type = BINDER_TYPE_BINDER, .binder = 0x1234, .cookie = 0x5678};
    CHECK(ioctl(server, BINDER_SET_CONTEXT_MGR_EXT, &manager) == 0);
    errno = 0;
    CHECK(ioctl(client, BINDER_SET_CONTEXT_MGR_EXT, &manager) == -1 && errno == EBUSY);
    pthread_t worker;
    CHECK(pthread_create(&worker, 0, serve, 0) == 0);
    for (int i = 0; i < 100; ++i) {
        struct binder_transaction_data tr = {.target.handle = 0, .code = 42, .data_size = sizeof i};
        tr.data.ptr.buffer = (uintptr_t)&i;
        send_command(client, BC_TRANSACTION, &tr);
        struct binder_transaction_data reply = receive_transaction(client, BR_REPLY);
        int value;
        CHECK(reply.data_size == sizeof value);
        memcpy(&value, (void*)(uintptr_t)reply.data.ptr.buffer, sizeof value);
        CHECK(value == i * 2);
        send_command(client, BC_FREE_BUFFER, &reply.data.ptr.buffer);
    }
    CHECK(pthread_join(worker, 0) == 0);
    close(client);
    close(server);
    puts("BINDER_SELFTEST_PASS");
    return 0;
}
