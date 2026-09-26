/* zlib for the guest: Android's libz.so is a system library, so its API is
   answered here. Decompression is a complete DEFLATE decoder (stored, fixed
   and dynamic Huffman blocks, written from RFC 1951, the way zlib's own
   contrib/puff does it) under the zlib (RFC 1950) and gzip (RFC 1952)
   wrappers. Compression writes stored blocks: valid DEFLATE that any reader
   takes, only not smaller. Unreal decompresses its pak entries with this.

   The guest's z_stream is the arm64 layout: next_in 0, avail_in 8,
   total_in 16, next_out 24, avail_out 32, total_out 40, msg 48, state 56,
   zalloc 64, zfree 72, opaque 80, data_type 88, adler 96, reserved 104. */
#ifndef QB_ZLIB_TEST
#include "android.h"
#include "qb_env.h"
#endif

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

const int Z_OK = 0, Z_STREAM_END = 1, Z_STREAM_ERROR = -2, Z_DATA_ERROR = -3, Z_BUF_ERROR = -5;
const int Z_FINISH = 4;

uint32_t adler32_of(uint32_t adler, const uint8_t* p, size_t n) {
    uint32_t a = adler & 0xffff, b = adler >> 16;
    for (size_t i = 0; i < n; ++i) {
        a = (a + p[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}
uint32_t crc32_of(uint32_t crc, const uint8_t* p, size_t n) {
    static uint32_t table[256];
    static bool made = [] {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        return true;
    }();
    (void)made;
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
    return ~crc;
}

/* ---- DEFLATE decoding ---- */

struct Huffman {
    uint16_t count[16];
    uint16_t symbol[320];
};

struct Inflater {
    const uint8_t* in;
    size_t in_size, pos = 0;
    uint32_t bits = 0;
    int bit_count = 0;
    std::vector<uint8_t>& out;
    bool short_input = false;
    Inflater(const uint8_t* data, size_t size, std::vector<uint8_t>& output) : in(data), in_size(size), out(output) {}

    int need(int n) {
        uint32_t value = bits;
        while (bit_count < n) {
            if (pos >= in_size) {
                short_input = true;
                return 0;
            }
            value |= (uint32_t)in[pos++] << bit_count;
            bit_count += 8;
        }
        bits = value >> n;
        bit_count -= n;
        return (int)(value & ((1u << n) - 1));
    }
    int decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            code |= need(1);
            if (short_input) return -1;
            int count = h.count[len];
            if (code - count < first) return h.symbol[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -10; /* ran out of codes */
    }
    static int build(Huffman& h, const uint16_t* lengths, int n) {
        std::memset(h.count, 0, sizeof(h.count));
        for (int s = 0; s < n; ++s) h.count[lengths[s]]++;
        if (h.count[0] == n) return 0;
        int left = 1;
        for (int len = 1; len < 16; ++len) {
            left <<= 1;
            left -= h.count[len];
            if (left < 0) return left;
        }
        uint16_t offs[16];
        offs[1] = 0;
        for (int len = 1; len < 15; ++len) offs[len + 1] = offs[len] + h.count[len];
        for (int s = 0; s < n; ++s)
            if (lengths[s]) h.symbol[offs[lengths[s]]++] = (uint16_t)s;
        return left;
    }
    int codes(const Huffman& lencode, const Huffman& distcode) {
        static const uint16_t lbase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                           31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const uint16_t lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const uint16_t dbase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                           193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static const uint16_t dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            int symbol = decode(lencode);
            if (symbol < 0) return short_input ? 2 : symbol;
            if (symbol < 256) {
                out.push_back((uint8_t)symbol);
            } else if (symbol == 256) {
                return 0;
            } else {
                symbol -= 257;
                if (symbol >= 29) return -10;
                int len = lbase[symbol] + need(lext[symbol]);
                int d = decode(distcode);
                if (d < 0) return short_input ? 2 : d;
                if (d >= 30) return -10;
                size_t dist = dbase[d] + (size_t)need(dext[d]);
                if (short_input) return 2;
                if (dist > out.size()) return -11;
                size_t from = out.size() - dist;
                for (int i = 0; i < len; ++i) out.push_back(out[from + i]);
            }
        }
    }
    int stored() {
        bits = 0;
        bit_count = 0;
        if (pos + 4 > in_size) return 2;
        unsigned len = in[pos] | (in[pos + 1] << 8);
        unsigned nlen = in[pos + 2] | (in[pos + 3] << 8);
        if (len != (~nlen & 0xffff)) return -2;
        pos += 4;
        if (pos + len > in_size) return 2;
        out.insert(out.end(), in + pos, in + pos + len);
        pos += len;
        return 0;
    }
    int fixed() {
        static Huffman lencode, distcode;
        static bool made = [] {
            uint16_t lengths[288];
            int s = 0;
            for (; s < 144; ++s) lengths[s] = 8;
            for (; s < 256; ++s) lengths[s] = 9;
            for (; s < 280; ++s) lengths[s] = 7;
            for (; s < 288; ++s) lengths[s] = 8;
            build(lencode, lengths, 288);
            for (s = 0; s < 30; ++s) lengths[s] = 5;
            build(distcode, lengths, 30);
            return true;
        }();
        (void)made;
        return codes(lencode, distcode);
    }
    int dynamic() {
        static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        uint16_t lengths[320];
        int nlen = need(5) + 257, ndist = need(5) + 1, ncode = need(4) + 4;
        if (short_input) return 2;
        if (nlen > 286 || ndist > 30) return -3;
        int index = 0;
        for (; index < ncode; ++index) lengths[order[index]] = (uint16_t)need(3);
        for (; index < 19; ++index) lengths[order[index]] = 0;
        if (short_input) return 2;
        Huffman lencode{}, distcode{};
        if (build(lencode, lengths, 19) != 0) return -4;
        index = 0;
        while (index < nlen + ndist) {
            int symbol = decode(lencode);
            if (symbol < 0) return short_input ? 2 : symbol;
            if (symbol < 16) {
                lengths[index++] = (uint16_t)symbol;
            } else {
                int len = 0, repeat;
                if (symbol == 16) {
                    if (index == 0) return -5;
                    len = lengths[index - 1];
                    repeat = 3 + need(2);
                } else if (symbol == 17) {
                    repeat = 3 + need(3);
                } else {
                    repeat = 11 + need(7);
                }
                if (short_input) return 2;
                if (index + repeat > nlen + ndist) return -6;
                while (repeat--) lengths[index++] = (uint16_t)len;
            }
        }
        if (lengths[256] == 0) return -9;
        int err = build(lencode, lengths, nlen);
        if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) return -7;
        err = build(distcode, lengths + nlen, ndist);
        if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1)) return -8;
        return codes(lencode, distcode);
    }
    /* 0 done, 2 the input stopped short, negative corrupt. */
    int run() {
        int last;
        do {
            last = need(1);
            int type = need(2);
            if (short_input) return 2;
            int err = type == 0 ? stored() : type == 1 ? fixed() : type == 2 ? dynamic() : -1;
            if (err != 0) return err;
        } while (!last);
        return 0;
    }
};

/* 1 complete, 0 more input needed, -1 corrupt. `used` is how much input the
   whole stream took, trailer included. */
int inflate_all(const uint8_t* in, size_t size, int window_bits, std::vector<uint8_t>& out, size_t& used) {
    size_t start = 0;
    /* windowBits: 8-15 zlib, 24-31 gzip, 40-47 either (by the header),
       negative raw DEFLATE. */
    bool zlib = false, gzip = false;
    if (window_bits >= 40) {
        gzip = size >= 2 && in[0] == 0x1f && in[1] == 0x8b;
        zlib = !gzip;
    } else if (window_bits >= 24) {
        gzip = true;
    } else if (window_bits > 0) {
        zlib = true;
    }
    if (zlib) {
        if (size < 2) return 0;
        if (((in[0] << 8) | in[1]) % 31 != 0 || (in[0] & 0x0f) != 8) return -1;
        start = 2 + ((in[1] & 0x20) ? 4 : 0);
    } else if (gzip) {
        if (size < 10) return 0;
        if (in[0] != 0x1f || in[1] != 0x8b || in[2] != 8) return -1;
        int flags = in[3];
        start = 10;
        if (flags & 4) {
            if (size < start + 2) return 0;
            start += 2 + (in[start] | (in[start + 1] << 8));
        }
        for (int field : {8, 16})
            if (flags & field) {
                while (start < size && in[start]) ++start;
                ++start;
            }
        if (flags & 2) start += 2;
        if (start > size) return 0;
    }
    out.clear();
    Inflater inflater(in + start, size - start, out);
    int result = inflater.run();
    if (result == 2) return 0;
    if (result < 0) return -1;
    used = start + inflater.pos + (zlib ? 4 : gzip ? 8 : 0);
    if (used > size) return 0; /* the trailer has not arrived */
    return 1;
}

/* Stored blocks: 65535 bytes at most each. */
void deflate_stored(const uint8_t* in, size_t size, int window_bits, std::vector<uint8_t>& out) {
    bool zlib = window_bits > 0 && window_bits <= 15, gzip = window_bits > 15;
    if (zlib) {
        out.push_back(0x78);
        out.push_back(0x01);
    } else if (gzip) {
        const uint8_t header[10] = {0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 3};
        out.insert(out.end(), header, header + 10);
    }
    size_t at = 0;
    do {
        size_t len = std::min<size_t>(65535, size - at);
        bool last = at + len == size;
        out.push_back(last ? 1 : 0);
        out.push_back((uint8_t)len);
        out.push_back((uint8_t)(len >> 8));
        out.push_back((uint8_t)~len);
        out.push_back((uint8_t)(~len >> 8));
        out.insert(out.end(), in + at, in + at + len);
        at += len;
    } while (at < size);
    if (zlib) {
        uint32_t a = adler32_of(1, in, size);
        for (int s = 24; s >= 0; s -= 8) out.push_back((uint8_t)(a >> s));
    } else if (gzip) {
        uint32_t c = crc32_of(0, in, size), n = (uint32_t)size;
        for (int s = 0; s < 32; s += 8) out.push_back((uint8_t)(c >> s));
        for (int s = 0; s < 32; s += 8) out.push_back((uint8_t)(n >> s));
    }
}

/* One stream's state, pointed to by z_stream.state: all input so far, the
   output made from it, and how much of that has been handed out. */
struct Stream {
    bool deflating = false;
    int window_bits = 15;
    std::vector<uint8_t> input, output;
    size_t delivered = 0;
    bool finished = false;
};

struct ZStream {
    uint8_t* p;
    uint64_t get(int off) const { uint64_t v; std::memcpy(&v, p + off, 8); return v; }
    uint32_t get32(int off) const { uint32_t v; std::memcpy(&v, p + off, 4); return v; }
    void set(int off, uint64_t v) { std::memcpy(p + off, &v, 8); }
    void set32(int off, uint32_t v) { std::memcpy(p + off, &v, 4); }
};

}  // namespace

#ifdef QB_ZLIB_TEST
int qb_test_inflate(const uint8_t* in, size_t size, int bits, std::vector<uint8_t>& out) {
    size_t used = 0;
    return inflate_all(in, size, bits, out, used);
}
void qb_test_deflate(const uint8_t* in, size_t size, int bits, std::vector<uint8_t>& out) { deflate_stored(in, size, bits, out); }
#else
bool GuestLibc::zlib_call(const std::string& name, GuestCpu& cpu) {
    auto arg = [&](int n) { return cpu.x[n]; };
    auto ret = [&](int64_t value) { cpu.x[0] = (uint64_t)value; };
    GuestMem& mem = image->mem;
    if (name == "zlibVersion") {
        static uint64_t text = guest_string("1.2.13");
        ret((int64_t)text);
        return true;
    }
    if (name == "compressBound" || name == "deflateBound") {
        uint64_t n = arg(name == "deflateBound" ? 1 : 0);
        ret((int64_t)(n + (n >> 12) + (n >> 14) + (n >> 25) + 13 + 5 * (n / 65535 + 1) + 18));
        return true;
    }
    if (name == "crc32" || name == "adler32" || name == "crc32_z" || name == "adler32_z") {
        const uint8_t* p = guest_ptr(mem, arg(1), arg(2) ? arg(2) : 1);
        bool crc = name[0] == 'c';
        if (!p) {
            ret(crc ? 0 : 1);
            return true;
        }
        ret(crc ? crc32_of((uint32_t)arg(0), p, (size_t)arg(2)) : adler32_of((uint32_t)arg(0), p, (size_t)arg(2)));
        return true;
    }
    if (name == "uncompress" || name == "uncompress2") {
        uint8_t* dest = guest_ptr(mem, arg(0), 1);
        uint8_t* dest_len_p = guest_ptr(mem, arg(1), 8);
        bool two = name == "uncompress2";
        uint64_t source_len = 0;
        if (two) {
            const uint8_t* sl = guest_ptr(mem, arg(3), 8);
            if (sl) std::memcpy(&source_len, sl, 8);
        } else {
            source_len = arg(3);
        }
        const uint8_t* source = guest_ptr(mem, arg(2), source_len ? source_len : 1);
        if (!dest || !dest_len_p || !source) {
            ret(Z_STREAM_ERROR);
            return true;
        }
        uint64_t room;
        std::memcpy(&room, dest_len_p, 8);
        std::vector<uint8_t> out;
        size_t used = 0;
        int status = inflate_all(source, (size_t)source_len, 15, out, used);
        if (status < 0) {
            ret(Z_DATA_ERROR);
            return true;
        }
        if (status == 0) {
            ret(Z_BUF_ERROR);
            return true;
        }
        if (out.size() > room) {
            ret(Z_BUF_ERROR);
            return true;
        }
        std::memcpy(dest, out.data(), out.size());
        uint64_t written = out.size();
        std::memcpy(dest_len_p, &written, 8);
        if (two) {
            uint64_t u = used;
            std::memcpy(guest_ptr(mem, arg(3), 8), &u, 8);
        }
        ret(Z_OK);
        return true;
    }
    if (name == "compress" || name == "compress2") {
        uint8_t* dest = guest_ptr(mem, arg(0), 1);
        uint8_t* dest_len_p = guest_ptr(mem, arg(1), 8);
        const uint8_t* source = guest_ptr(mem, arg(2), arg(3) ? arg(3) : 1);
        if (!dest || !dest_len_p || !source) {
            ret(Z_STREAM_ERROR);
            return true;
        }
        std::vector<uint8_t> out;
        deflate_stored(source, (size_t)arg(3), 15, out);
        uint64_t room;
        std::memcpy(&room, dest_len_p, 8);
        if (out.size() > room) {
            ret(Z_BUF_ERROR);
            return true;
        }
        std::memcpy(dest, out.data(), out.size());
        uint64_t written = out.size();
        std::memcpy(dest_len_p, &written, 8);
        ret(Z_OK);
        return true;
    }
    /* The streaming API. */
    uint8_t* z = guest_ptr(mem, arg(0), 112);
    if (!z) {
        ret(Z_STREAM_ERROR);
        return true;
    }
    ZStream zs{z};
    auto state = [&]() { return reinterpret_cast<Stream*>(zs.get(56)); };
    if (name == "inflateInit_" || name == "inflateInit2_" || name == "deflateInit_" || name == "deflateInit2_") {
        auto* s = new Stream;
        s->deflating = name[0] == 'd';
        if (name == "inflateInit2_") s->window_bits = (int)(int32_t)arg(1);
        if (name == "deflateInit2_") s->window_bits = (int)(int32_t)arg(3);
        zs.set(56, reinterpret_cast<uint64_t>(s));
        zs.set(16, 0);
        zs.set(40, 0);
        zs.set(48, 0);
        zs.set(96, s->window_bits > 15 ? 0 : 1);
        ret(Z_OK);
        return true;
    }
    if (name == "inflateEnd" || name == "deflateEnd") {
        delete state();
        zs.set(56, 0);
        ret(Z_OK);
        return true;
    }
    if (name == "inflateReset" || name == "deflateReset" || name == "inflateReset2") {
        Stream* s = state();
        if (!s) {
            ret(Z_STREAM_ERROR);
            return true;
        }
        int bits = name == "inflateReset2" ? (int)(int32_t)arg(1) : s->window_bits;
        bool deflating = s->deflating;
        *s = Stream{};
        s->deflating = deflating;
        s->window_bits = bits;
        zs.set(16, 0);
        zs.set(40, 0);
        ret(Z_OK);
        return true;
    }
    if (name == "inflate" || name == "deflate") {
        Stream* s = state();
        if (!s) {
            ret(Z_STREAM_ERROR);
            return true;
        }
        int flush = (int)(int32_t)arg(1);
        uint64_t next_in = zs.get(0), next_out = zs.get(24);
        uint32_t avail_in = zs.get32(8), avail_out = zs.get32(32);
        /* All the input offered is taken now and kept. */
        if (avail_in && !s->finished) {
            const uint8_t* in = guest_ptr(mem, next_in, avail_in);
            if (!in) {
                ret(Z_STREAM_ERROR);
                return true;
            }
            s->input.insert(s->input.end(), in, in + avail_in);
            zs.set(0, next_in + avail_in);
            zs.set32(8, 0);
            zs.set(16, zs.get(16) + avail_in);
        }
        if (!s->finished) {
            if (s->deflating) {
                if (flush == Z_FINISH) {
                    deflate_stored(s->input.data(), s->input.size(), s->window_bits, s->output);
                    s->finished = true;
                }
            } else {
                size_t used = 0;
                std::vector<uint8_t> out;
                int status = inflate_all(s->input.data(), s->input.size(), s->window_bits, out, used);
                if (status < 0) {
                    ret(Z_DATA_ERROR);
                    return true;
                }
                if (status == 1) {
                    s->output.swap(out);
                    s->finished = true;
                    /* Input past the stream's end goes back to the caller. */
                    size_t extra = s->input.size() - used;
                    if (extra) {
                        zs.set(0, zs.get(0) - extra);
                        zs.set32(8, (uint32_t)extra);
                        zs.set(16, zs.get(16) - extra);
                    }
                    zs.set(96, s->window_bits > 15 ? crc32_of(0, s->output.data(), s->output.size())
                                                   : adler32_of(1, s->output.data(), s->output.size()));
                }
            }
        }
        size_t give = std::min<size_t>(avail_out, s->output.size() - s->delivered);
        if (give) {
            uint8_t* out = guest_ptr(mem, next_out, give);
            if (!out) {
                ret(Z_STREAM_ERROR);
                return true;
            }
            std::memcpy(out, s->output.data() + s->delivered, give);
            s->delivered += give;
            zs.set(24, next_out + give);
            zs.set32(32, avail_out - (uint32_t)give);
            zs.set(40, zs.get(40) + give);
        }
        if (s->finished && s->delivered == s->output.size()) ret(Z_STREAM_END);
        else if (!give && !avail_in) ret(Z_BUF_ERROR);
        else ret(Z_OK);
        return true;
    }
    return false;
}
#endif
