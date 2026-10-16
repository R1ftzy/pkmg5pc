#include "pkmn/core/compression.h"

#include <algorithm>
#include <array>
#include <deque>
#include <queue>
#include <stdexcept>
#include <string>
#include <utility>

namespace pkmn::compression {
namespace {

    [[noreturn]] void fail(const char* msg) { throw std::runtime_error(msg); }

    void need(usize have, usize idx, usize n, const char* what)
    {
        if (idx + n > have) {
            throw std::runtime_error(std::string("truncated ") + what);
        }
    }

    u32 rd24(const u8* p)
    {
        return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8)
            | (static_cast<u32>(p[2]) << 16);
    }

    u32 rd32(const u8* p) { return rd24(p) | (static_cast<u32>(p[3]) << 24); }

    void write_size24(std::vector<u8>& out, u32 size)
    {
        out.push_back(static_cast<u8>(size & 0xFF));
        out.push_back(static_cast<u8>((size >> 8) & 0xFF));
        out.push_back(static_cast<u8>((size >> 16) & 0xFF));
    }

    struct Header {
        u32 size;
        usize pos;
    };

    Header parse_header(byte_span in, u8 expect)
    {
        if (in.size() < 4) {
            fail("compression header too short");
        }
        if (in[0] != expect) {
            fail("unexpected compression type");
        }
        u32 size = rd24(in.data() + 1);
        usize pos = 4;
        if (size == 0) {
            need(in.size(), 4, 4, "extended size");
            size = rd32(in.data() + 4);
            pos = 8;
        }
        return Header { size, pos };
    }

    struct Match {
        usize len;
        usize disp;
    };

    Match longest_match(byte_span in, usize pos, usize max_len, usize window)
    {
        usize old = std::min(pos, window);
        usize limit = std::min(max_len, in.size() - pos);
        usize best_len = 0;
        usize best_disp = 0;
        for (usize d = 1; d <= old; ++d) {
            usize len = 0;
            while (len < limit && in[pos - d + len] == in[pos + len]) {
                ++len;
            }
            if (len > best_len) {
                best_len = len;
                best_disp = d;
                if (best_len == limit) {
                    break;
                }
            }
        }
        return Match { best_len, best_disp };
    }

    struct HuffNode {
        int c0 = -1;
        int c1 = -1;
        bool is_data = false;
        u8 data = 0;
    };

    int read_huff_node(
        byte_span in, usize addr, bool is_data, usize max_pos, std::vector<HuffNode>& nodes)
    {
        if (nodes.size() > 0x2000) {
            fail("huffman tree too large");
        }
        int idx = static_cast<int>(nodes.size());
        nodes.push_back(HuffNode {});
        if (addr >= max_pos) {
            return idx;
        }
        if (addr >= in.size()) {
            fail("huffman tree out of bounds");
        }
        u8 val = in[addr];
        nodes[idx].is_data = is_data;
        if (is_data) {
            nodes[idx].data = val;
            return idx;
        }
        usize offset = val & 0x3F;
        bool zero_is_data = (val & 0x80) != 0;
        bool one_is_data = (val & 0x40) != 0;
        usize zero_addr = (addr ^ (addr & 1U)) + offset * 2 + 2;
        int c0 = read_huff_node(in, zero_addr, zero_is_data, max_pos, nodes);
        int c1 = read_huff_node(in, zero_addr + 1, one_is_data, max_pos, nodes);
        nodes[idx].c0 = c0;
        nodes[idx].c1 = c1;
        return idx;
    }

    void append_le32(std::vector<u8>& out, u32 v)
    {
        out.push_back(static_cast<u8>(v & 0xFF));
        out.push_back(static_cast<u8>((v >> 8) & 0xFF));
        out.push_back(static_cast<u8>((v >> 16) & 0xFF));
        out.push_back(static_cast<u8>((v >> 24) & 0xFF));
    }

    std::vector<u8> huffman_compress(byte_span in, int block_bits)
    {
        if (in.empty()) {
            fail("cannot huffman-compress empty input");
        }
        int n_sym = block_bits == 4 ? 16 : 256;
        std::vector<u64> freq(static_cast<usize>(n_sym), 0);
        if (block_bits == 4) {
            for (u8 b : in) {
                freq[b & 0x0F]++;
                freq[b >> 4]++;
            }
        } else {
            for (u8 b : in) {
                freq[b]++;
            }
        }

        struct Build {
            u64 freq;
            int id;
        };
        auto cmp = [](const Build& a, const Build& b) {
            if (a.freq != b.freq) {
                return a.freq > b.freq;
            }
            return a.id > b.id;
        };
        std::priority_queue<Build, std::vector<Build>, decltype(cmp)> pq(cmp);
        std::vector<HuffNode> nodes;
        for (int s = 0; s < n_sym; ++s) {
            if (freq[static_cast<usize>(s)] == 0) {
                continue;
            }
            HuffNode leaf;
            leaf.is_data = true;
            leaf.data = static_cast<u8>(s);
            int id = static_cast<int>(nodes.size());
            nodes.push_back(leaf);
            pq.push(Build { freq[static_cast<usize>(s)], id });
        }
        if (pq.size() < 2) {
            fail("huffman requires at least two distinct symbols");
        }
        while (pq.size() > 1) {
            Build a = pq.top();
            pq.pop();
            Build b = pq.top();
            pq.pop();
            HuffNode parent;
            parent.c0 = a.id;
            parent.c1 = b.id;
            int id = static_cast<int>(nodes.size());
            nodes.push_back(parent);
            pq.push(Build { a.freq + b.freq, id });
        }
        int root = static_cast<int>(nodes.size()) - 1;

        std::vector<std::vector<bool>> codes(static_cast<usize>(n_sym));
        std::vector<std::pair<int, std::vector<bool>>> stack;
        stack.push_back({ root, {} });
        while (!stack.empty()) {
            auto entry = std::move(stack.back());
            stack.pop_back();
            int id = entry.first;
            if (nodes[static_cast<usize>(id)].is_data) {
                codes[nodes[static_cast<usize>(id)].data] = std::move(entry.second);
                continue;
            }
            std::vector<bool> p0 = entry.second;
            p0.push_back(false);
            std::vector<bool> p1 = entry.second;
            p1.push_back(true);
            stack.push_back({ nodes[static_cast<usize>(id)].c0, std::move(p0) });
            stack.push_back({ nodes[static_cast<usize>(id)].c1, std::move(p1) });
        }

        std::vector<u8> out;
        out.push_back(static_cast<u8>(block_bits == 4 ? 0x24 : 0x28));
        write_size24(out, static_cast<u32>(in.size()));
        int node_count = static_cast<int>(nodes.size());
        out.push_back(static_cast<u8>((node_count - 1) / 2));

        std::deque<int> q;
        q.push_back(root);
        while (!q.empty()) {
            int id = q.front();
            q.pop_front();
            if (nodes[static_cast<usize>(id)].is_data) {
                out.push_back(nodes[static_cast<usize>(id)].data);
                continue;
            }
            usize offset = q.size() / 2;
            if (offset > 0x3F) {
                fail("huffman tree too wide for this serializer");
            }
            u8 byte = static_cast<u8>(offset & 0x3F);
            if (nodes[static_cast<usize>(nodes[static_cast<usize>(id)].c0)].is_data) {
                byte |= 0x80;
            }
            if (nodes[static_cast<usize>(nodes[static_cast<usize>(id)].c1)].is_data) {
                byte |= 0x40;
            }
            out.push_back(byte);
            q.push_back(nodes[static_cast<usize>(id)].c0);
            q.push_back(nodes[static_cast<usize>(id)].c1);
        }

        u32 block = 0;
        int bits_left = 32;
        auto emit = [&](int sym) {
            for (bool bit : codes[static_cast<usize>(sym)]) {
                if (bits_left == 0) {
                    append_le32(out, block);
                    block = 0;
                    bits_left = 32;
                }
                --bits_left;
                if (bit) {
                    block |= (1U << static_cast<unsigned>(bits_left));
                }
            }
        };
        if (block_bits == 4) {
            for (u8 b : in) {
                emit(b >> 4);
                emit(b & 0x0F);
            }
        } else {
            for (u8 b : in) {
                emit(b);
            }
        }
        if (bits_left != 32) {
            append_le32(out, block);
        }
        return out;
    }

}

std::optional<Format> detect(byte_span in)
{
    if (in.empty()) {
        return std::nullopt;
    }
    switch (in[0]) {
    case 0x10:
        return Format::lz10;
    case 0x11:
        return Format::lz11;
    case 0x24:
        return Format::huffman4;
    case 0x28:
        return Format::huffman8;
    case 0x30:
        return Format::rle;
    default:
        return std::nullopt;
    }
}

u32 decompressed_size(byte_span in)
{
    if (in.size() < 4) {
        fail("compression header too short");
    }
    u32 size = rd24(in.data() + 1);
    if (size == 0) {
        if (in.size() < 8) {
            fail("extended size header too short");
        }
        size = rd32(in.data() + 4);
    }
    return size;
}

std::vector<u8> decompress(byte_span in)
{
    if (in.empty()) {
        fail("empty input");
    }
    switch (in[0]) {
    case 0x10:
        return lz10_decompress(in);
    case 0x11:
        return lz11_decompress(in);
    case 0x24:
    case 0x28:
        return huffman_decompress(in);
    case 0x30:
        return rle_decompress(in);
    default:
        fail("unknown compression type");
    }
}

std::vector<u8> lz10_decompress(byte_span in)
{
    Header h = parse_header(in, 0x10);
    std::vector<u8> out;
    out.reserve(h.size);
    usize pos = h.pos;
    while (out.size() < h.size) {
        need(in.size(), pos, 1, "lz10 flags");
        u8 flags = in[pos++];
        for (int i = 0; i < 8 && out.size() < h.size; ++i) {
            if (flags & (0x80U >> static_cast<unsigned>(i))) {
                need(in.size(), pos, 2, "lz10 block");
                u8 b1 = in[pos++];
                u8 b2 = in[pos++];
                usize len = static_cast<usize>(b1 >> 4) + 3;
                usize disp = ((static_cast<usize>(b1) & 0x0F) << 8) | b2;
                if (disp + 1 > out.size()) {
                    fail("lz10 displacement out of range");
                }
                usize src = out.size() - disp - 1;
                for (usize k = 0; k < len && out.size() < h.size; ++k) {
                    out.push_back(out[src + k]);
                }
            } else {
                need(in.size(), pos, 1, "lz10 literal");
                out.push_back(in[pos++]);
            }
        }
    }
    return out;
}

std::vector<u8> lz11_decompress(byte_span in)
{
    Header h = parse_header(in, 0x11);
    std::vector<u8> out;
    out.reserve(h.size);
    usize pos = h.pos;
    u8 flags = 0;
    unsigned mask = 1;
    while (out.size() < h.size) {
        if (mask == 1) {
            need(in.size(), pos, 1, "lz11 flags");
            flags = in[pos++];
            mask = 0x80;
        } else {
            mask >>= 1;
        }
        if (flags & mask) {
            need(in.size(), pos, 1, "lz11 block");
            u8 b1 = in[pos++];
            usize length;
            usize disp;
            unsigned indicator = static_cast<unsigned>(b1 >> 4);
            if (indicator == 0) {
                need(in.size(), pos, 2, "lz11 block0");
                u8 b2 = in[pos++];
                u8 b3 = in[pos++];
                length
                    = ((static_cast<usize>(b1) & 0x0F) << 4) + static_cast<usize>(b2 >> 4) + 0x11;
                disp = ((static_cast<usize>(b2) & 0x0F) << 8) | b3;
            } else if (indicator == 1) {
                need(in.size(), pos, 3, "lz11 block1");
                u8 b2 = in[pos++];
                u8 b3 = in[pos++];
                u8 b4 = in[pos++];
                length = ((static_cast<usize>(b1) & 0x0F) << 12) + (static_cast<usize>(b2) << 4)
                    + static_cast<usize>(b3 >> 4) + 0x111;
                disp = ((static_cast<usize>(b3) & 0x0F) << 8) | b4;
            } else {
                need(in.size(), pos, 1, "lz11 blockn");
                u8 b2 = in[pos++];
                length = static_cast<usize>(indicator) + 1;
                disp = ((static_cast<usize>(b1) & 0x0F) << 8) | b2;
            }
            if (disp + 1 > out.size()) {
                fail("lz11 displacement out of range");
            }
            usize src = out.size() - disp - 1;
            for (usize k = 0; k < length && out.size() < h.size; ++k) {
                out.push_back(out[src + k]);
            }
        } else {
            need(in.size(), pos, 1, "lz11 literal");
            out.push_back(in[pos++]);
        }
    }
    return out;
}

std::vector<u8> rle_decompress(byte_span in)
{
    Header h = parse_header(in, 0x30);
    std::vector<u8> out;
    out.reserve(h.size);
    usize pos = h.pos;
    while (out.size() < h.size) {
        need(in.size(), pos, 1, "rle flag");
        u8 flag = in[pos++];
        bool compressed = (flag & 0x80) != 0;
        usize len = static_cast<usize>(flag & 0x7F) + (compressed ? 3 : 1);
        if (compressed) {
            need(in.size(), pos, 1, "rle data");
            u8 value = in[pos++];
            usize take = std::min(len, h.size - out.size());
            out.insert(out.end(), take, value);
        } else {
            need(in.size(), pos, len, "rle data");
            usize take = std::min(len, h.size - out.size());
            out.insert(out.end(), in.begin() + static_cast<std::ptrdiff_t>(pos),
                in.begin() + static_cast<std::ptrdiff_t>(pos + take));
            pos += take;
        }
    }
    return out;
}

std::vector<u8> huffman_decompress(byte_span in)
{
    if (in.empty()) {
        fail("empty input");
    }
    int block_bits;
    if (in[0] == 0x24) {
        block_bits = 4;
    } else if (in[0] == 0x28) {
        block_bits = 8;
    } else {
        fail("not a huffman stream");
    }
    Header h = parse_header(in, in[0]);
    usize pos = h.pos;
    need(in.size(), pos, 1, "huffman tree size");
    u8 tree_size_byte = in[pos++];
    usize tree_size = (static_cast<usize>(tree_size_byte) + 1) * 2;
    usize tree_start = pos;
    usize max_pos = tree_start + tree_size - 1;

    std::vector<HuffNode> nodes;
    int root = read_huff_node(in, tree_start, false, max_pos, nodes);

    usize bit_bytes = max_pos;
    u32 data = 0;
    int bits_left = 0;
    auto next_bit = [&]() -> bool {
        if (bits_left == 0) {
            if (bit_bytes + 4 > in.size()) {
                fail("huffman bitstream truncated");
            }
            data = rd32(in.data() + bit_bytes);
            bit_bytes += 4;
            bits_left = 32;
        }
        --bits_left;
        return ((data >> static_cast<unsigned>(bits_left)) & 1U) != 0;
    };

    std::vector<u8> out;
    out.reserve(h.size);
    int cached = -1;
    while (out.size() < h.size) {
        int node = root;
        while (!nodes[static_cast<usize>(node)].is_data) {
            bool bit = next_bit();
            node = bit ? nodes[static_cast<usize>(node)].c1 : nodes[static_cast<usize>(node)].c0;
            if (node < 0) {
                fail("huffman tree traversal failed");
            }
        }
        u8 symbol = nodes[static_cast<usize>(node)].data;
        if (block_bits == 8) {
            out.push_back(symbol);
        } else {
            if (cached < 0) {
                cached = static_cast<int>(symbol) << 4;
            } else {
                out.push_back(static_cast<u8>(cached | symbol));
                cached = -1;
            }
        }
    }
    return out;
}

std::vector<u8> blz_decompress(byte_span in)
{
    if (in.size() < 4) {
        fail("blz too short");
    }
    std::vector<u8> buf(in.begin(), in.end());
    const usize total = buf.size();

    u32 inc_len = rd32(buf.data() + total - 4);
    usize dec_len = 0;
    usize payload_len = 0;
    usize raw_len = 0;
    if (inc_len == 0) {
        dec_len = total - 4;
        payload_len = 0;
        raw_len = dec_len;
    } else {
        if (total < 8) {
            fail("blz bad header");
        }
        u8 hdr_len = buf[total - 5];
        if (hdr_len < 0x08 || hdr_len > 0x0B) {
            fail("blz bad header length");
        }
        if (total <= hdr_len) {
            fail("blz bad length");
        }
        u32 enc_len = rd32(buf.data() + total - 8) & 0x00FFFFFF;
        if (enc_len > total || enc_len < hdr_len) {
            fail("blz bad encoded length");
        }
        dec_len = total - enc_len;
        payload_len = enc_len - hdr_len;
        raw_len = dec_len + enc_len + inc_len;
    }
    if (raw_len > 0x08000000) {
        fail("blz decoded length too large");
    }
    if (dec_len + payload_len > total) {
        fail("blz overflow");
    }

    std::vector<u8> out(raw_len, 0);
    std::copy_n(buf.begin(), static_cast<std::ptrdiff_t>(dec_len), out.begin());
    std::reverse(buf.begin() + static_cast<std::ptrdiff_t>(dec_len),
        buf.begin() + static_cast<std::ptrdiff_t>(dec_len + payload_len));

    usize pak = dec_len;
    const usize pak_end = dec_len + payload_len;
    usize raw = dec_len;
    const usize raw_end = raw_len;

    u32 mask = 0;
    u8 flags = 0;
    while (raw < raw_end) {
        if (!(mask >>= 1)) {
            if (pak == pak_end) {
                break;
            }
            flags = buf[pak++];
            mask = 0x80;
        }
        if (!(flags & mask)) {
            if (pak == pak_end) {
                break;
            }
            out[raw++] = buf[pak++];
        } else {
            if (pak + 1 >= pak_end) {
                break;
            }
            u32 pos = (static_cast<u32>(buf[pak]) << 8) | buf[pak + 1];
            pak += 2;
            usize len = (pos >> 12) + 3;
            if (raw + len > raw_end) {
                len = raw_end - raw;
            }
            usize dist = (pos & 0x0FFF) + 3;
            if (dist > raw) {
                fail("blz displacement out of range");
            }
            while (len-- > 0) {
                out[raw] = out[raw - dist];
                ++raw;
            }
        }
    }

    std::reverse(out.begin() + static_cast<std::ptrdiff_t>(dec_len),
        out.begin() + static_cast<std::ptrdiff_t>(raw_len));
    out.resize(raw);
    return out;
}

std::vector<u8> lz10_compress(byte_span in)
{
    if (in.size() > 0xFFFFFF) {
        fail("input too large for lz10");
    }
    std::vector<u8> out;
    out.push_back(0x10);
    write_size24(out, static_cast<u32>(in.size()));

    std::array<u8, 17> buf {};
    buf[0] = 0;
    usize buf_len = 1;
    int blocks = 0;
    usize read = 0;
    while (read < in.size()) {
        if (blocks == 8) {
            out.insert(out.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(buf_len));
            buf[0] = 0;
            buf_len = 1;
            blocks = 0;
        }
        Match m = longest_match(in, read, 0x12, 0x1000);
        if (m.len < 3) {
            buf[buf_len++] = in[read++];
        } else {
            read += m.len;
            buf[0] = static_cast<u8>(buf[0] | (1U << (7 - blocks)));
            buf[buf_len] = static_cast<u8>(((m.len - 3) << 4) & 0xF0);
            buf[buf_len] = static_cast<u8>(buf[buf_len] | (((m.disp - 1) >> 8) & 0x0F));
            ++buf_len;
            buf[buf_len] = static_cast<u8>((m.disp - 1) & 0xFF);
            ++buf_len;
        }
        ++blocks;
    }
    if (blocks > 0) {
        out.insert(out.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(buf_len));
    }
    return out;
}

std::vector<u8> lz11_compress(byte_span in)
{
    if (in.size() > 0xFFFFFF) {
        fail("input too large for lz11");
    }
    std::vector<u8> out;
    out.push_back(0x11);
    write_size24(out, static_cast<u32>(in.size()));

    std::array<u8, 33> buf {};
    buf[0] = 0;
    usize buf_len = 1;
    int blocks = 0;
    usize read = 0;
    while (read < in.size()) {
        if (blocks == 8) {
            out.insert(out.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(buf_len));
            buf[0] = 0;
            buf_len = 1;
            blocks = 0;
        }
        Match m = longest_match(in, read, 0x10110, 0x1000);
        if (m.len < 3) {
            buf[buf_len++] = in[read++];
        } else {
            read += m.len;
            buf[0] = static_cast<u8>(buf[0] | (1U << (7 - blocks)));
            usize len = m.len;
            usize disp = m.disp;
            if (len > 0x110) {
                u32 l = static_cast<u32>(len - 0x111);
                u32 d = static_cast<u32>(disp - 1);
                buf[buf_len++] = static_cast<u8>(0x10 | ((l >> 12) & 0x0F));
                buf[buf_len++] = static_cast<u8>((l >> 4) & 0xFF);
                buf[buf_len++] = static_cast<u8>(((l & 0x0F) << 4) | ((d >> 8) & 0x0F));
                buf[buf_len++] = static_cast<u8>(d & 0xFF);
            } else if (len > 0x10) {
                u32 l = static_cast<u32>(len - 0x11);
                u32 d = static_cast<u32>(disp - 1);
                buf[buf_len++] = static_cast<u8>((l >> 4) & 0x0F);
                buf[buf_len++] = static_cast<u8>(((l & 0x0F) << 4) | ((d >> 8) & 0x0F));
                buf[buf_len++] = static_cast<u8>(d & 0xFF);
            } else {
                u32 d = static_cast<u32>(disp - 1);
                buf[buf_len++] = static_cast<u8>(((len - 1) << 4) | ((d >> 8) & 0x0F));
                buf[buf_len++] = static_cast<u8>(d & 0xFF);
            }
        }
        ++blocks;
    }
    if (blocks > 0) {
        out.insert(out.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(buf_len));
    }
    return out;
}

std::vector<u8> rle_compress(byte_span in)
{
    if (in.size() > 0xFFFFFF) {
        fail("input too large for rle");
    }
    std::vector<u8> body;
    usize i = 0;
    while (i < in.size()) {
        usize run = 1;
        while (i + run < in.size() && in[i + run] == in[i] && run < 130) {
            ++run;
        }
        if (run >= 3) {
            body.push_back(static_cast<u8>(0x80 | (run - 3)));
            body.push_back(in[i]);
            i += run;
        } else {
            usize start = i;
            usize len = 0;
            while (i < in.size() && len < 128) {
                usize r = 1;
                while (i + r < in.size() && in[i + r] == in[i] && r < 130) {
                    ++r;
                }
                if (r >= 3) {
                    break;
                }
                ++i;
                ++len;
            }
            body.push_back(static_cast<u8>(len - 1));
            for (usize k = 0; k < len; ++k) {
                body.push_back(in[start + k]);
            }
        }
    }
    std::vector<u8> out;
    out.push_back(0x30);
    write_size24(out, static_cast<u32>(in.size()));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

std::vector<u8> huffman4_compress(byte_span in) { return huffman_compress(in, 4); }

std::vector<u8> huffman8_compress(byte_span in) { return huffman_compress(in, 8); }

}
