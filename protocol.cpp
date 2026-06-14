#include "protocol.h"

#include <cstring>
#include <arpa/inet.h>

static uint32_t crc_table[256];
static bool crc_init = false;

static void init_crc()
{
    for (uint32_t i = 0; i < 256; i++)
    {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
        crc_table[i] = c;
    }
    crc_init = true;
}

uint32_t crc32(const uint8_t *buf, size_t len)
{
    if (!crc_init)
        init_crc();
    uint32_t c = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++)
        c = crc_table[(c ^ buf[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFF;
}

uint32_t crc32(const std::vector<uint8_t> &v)
{
    return crc32(v.data(), v.size());
}

std::vector<uint8_t> serializePacket(const Packet &pkt)
{
    PacketHeader h = pkt.header;
    h.seq = htonl(h.seq);
    h.total_chunks = htonl(h.total_chunks);
    h.payload_len = htons(h.payload_len);
    h.checksum = htonl(h.checksum);

    std::vector<uint8_t> buf(sizeof(PacketHeader) + pkt.payload.size());
    std::memcpy(buf.data(), &h, sizeof(PacketHeader));
    if (!pkt.payload.empty())
        std::memcpy(buf.data() + sizeof(PacketHeader), pkt.payload.data(), pkt.payload.size());
    return buf;
}

bool deserializePacket(const std::vector<uint8_t> &buf, Packet &pkt)
{
    if (buf.size() < sizeof(PacketHeader))
        return false;

    std::memcpy(&pkt.header, buf.data(), sizeof(PacketHeader));

    pkt.header.seq = ntohl(pkt.header.seq);
    pkt.header.total_chunks = ntohl(pkt.header.total_chunks);
    pkt.header.payload_len = ntohs(pkt.header.payload_len);
    pkt.header.checksum = ntohl(pkt.header.checksum);

    if (sizeof(PacketHeader) + pkt.header.payload_len > buf.size())
        return false;

    pkt.payload.resize(pkt.header.payload_len);
    if (pkt.header.payload_len > 0)
        std::memcpy(pkt.payload.data(), buf.data() + sizeof(PacketHeader), pkt.header.payload_len);

    return true;
}

Packet makeHelloPacket(const std::string &filename, uint32_t total_chunks, uint32_t file_crc32)
{
    Packet pkt{};
    pkt.header.version = PROTO_VERSION;
    pkt.header.type = static_cast<uint8_t>(PacketType::HELLO);
    pkt.header.seq = 0;
    pkt.header.total_chunks = total_chunks;
    pkt.payload = buildHelloPayload(filename, total_chunks, file_crc32);
    pkt.header.payload_len = pkt.payload.size();
    pkt.header.checksum = crc32(pkt.payload);
    return pkt;
}

Packet makeHelloAckPacket(const std::vector<uint8_t> &optional_bitmap)
{
    Packet pkt{};
    pkt.header.version = PROTO_VERSION;
    pkt.header.type = static_cast<uint8_t>(PacketType::HELLO_ACK);
    pkt.header.seq = 0;
    pkt.header.total_chunks = 0;
    pkt.payload = optional_bitmap;
    pkt.header.payload_len = pkt.payload.size();
    pkt.header.checksum = crc32(pkt.payload);
    return pkt;
}

Packet makeDataPacket(uint32_t seq, uint32_t total_chunks, const std::vector<uint8_t> &payload, uint32_t payload_crc32)
{
    Packet pkt{};
    pkt.header.version = PROTO_VERSION;
    pkt.header.type = static_cast<uint8_t>(PacketType::DATA);
    pkt.header.seq = seq;
    pkt.header.total_chunks = total_chunks;
    pkt.payload = payload;
    pkt.header.payload_len = payload.size();
    pkt.header.checksum = payload_crc32;
    return pkt;
}

Packet makeAckPacket(uint32_t base_seq, const std::vector<uint8_t> &bitmap)
{
    Packet pkt{};
    pkt.header.version = PROTO_VERSION;
    pkt.header.type = static_cast<uint8_t>(PacketType::ACK);
    pkt.header.seq = base_seq;
    pkt.header.total_chunks = 0;
    pkt.payload = bitmap;
    pkt.header.payload_len = bitmap.size();
    pkt.header.checksum = crc32(pkt.payload);
    return pkt;
}

Packet makeNackPacket(const std::vector<SeqRange> &missing_ranges)
{
    Packet pkt{};
    pkt.header.version = PROTO_VERSION;
    pkt.header.type = static_cast<uint8_t>(PacketType::NACK);
    pkt.header.seq = 0;
    pkt.header.total_chunks = 0;

    pkt.payload.clear();
    for (const auto &r : missing_ranges)
    {
        uint32_t s = htonl(r.first);
        uint32_t l = htonl(r.second);
        pkt.payload.insert(pkt.payload.end(), reinterpret_cast<uint8_t *>(&s), reinterpret_cast<uint8_t *>(&s) + 4);
        pkt.payload.insert(pkt.payload.end(), reinterpret_cast<uint8_t *>(&l), reinterpret_cast<uint8_t *>(&l) + 4);
    }

    pkt.header.payload_len = pkt.payload.size();
    pkt.header.checksum = crc32(pkt.payload);
    return pkt;
}

Packet makeFinPacket(uint32_t file_crc32)
{
    Packet pkt{};
    pkt.header.version = PROTO_VERSION;
    pkt.header.type = static_cast<uint8_t>(PacketType::FIN);
    pkt.header.seq = 0;
    pkt.header.total_chunks = 0;
    pkt.payload.resize(4);
    uint32_t c = htonl(file_crc32);
    std::memcpy(pkt.payload.data(), &c, 4);
    pkt.header.payload_len = 4;
    pkt.header.checksum = crc32(pkt.payload);
    return pkt;
}

Packet makeFinAckPacket()
{
    Packet pkt{};
    pkt.header.version = PROTO_VERSION;
    pkt.header.type = static_cast<uint8_t>(PacketType::FIN_ACK);
    pkt.header.seq = 0;
    pkt.header.total_chunks = 0;
    pkt.header.payload_len = 0;
    pkt.header.checksum = 0;
    return pkt;
}

std::vector<uint8_t> encodeBitmap(const std::vector<bool> &received, size_t window_size)
{
    size_t bytes = (window_size + 7) / 8;
    std::vector<uint8_t> bitmap(bytes, 0);
    for (size_t i = 0; i < window_size && i < received.size(); i++)
        if (received[i])
            bitmap[i / 8] |= (1 << (i % 8));
    return bitmap;
}

std::vector<bool> decodeBitmap(const std::vector<uint8_t> &bitmap, size_t window_size)
{
    std::vector<bool> received(window_size, false);
    for (size_t i = 0; i < window_size; i++)
        if (bitmap[i / 8] & (1 << (i % 8)))
            received[i] = true;
    return received;
}

std::vector<uint8_t> buildHelloPayload(const std::string &filename, uint32_t total_chunks, uint32_t file_crc32)
{
    std::vector<uint8_t> p;
    uint16_t name_len = htons(filename.size());
    uint32_t tc = htonl(total_chunks);
    uint32_t fc = htonl(file_crc32);

    p.insert(p.end(), reinterpret_cast<uint8_t *>(&name_len), reinterpret_cast<uint8_t *>(&name_len) + 2);
    p.insert(p.end(), filename.begin(), filename.end());
    p.insert(p.end(), reinterpret_cast<uint8_t *>(&tc), reinterpret_cast<uint8_t *>(&tc) + 4);
    p.insert(p.end(), reinterpret_cast<uint8_t *>(&fc), reinterpret_cast<uint8_t *>(&fc) + 4);

    return p;
}

bool parseHelloPayload(const std::vector<uint8_t> &payload, std::string &filename, uint32_t &total_chunks, uint32_t &file_crc32)
{
    if (payload.size() < 10)
        return false;

    uint16_t name_len;
    std::memcpy(&name_len, payload.data(), 2);
    name_len = ntohs(name_len);

    if (payload.size() < 2 + name_len + 8)
        return false;

    filename.assign(reinterpret_cast<const char *>(payload.data() + 2), name_len);

    std::memcpy(&total_chunks, payload.data() + 2 + name_len, 4);
    std::memcpy(&file_crc32, payload.data() + 6 + name_len, 4);

    total_chunks = ntohl(total_chunks);
    file_crc32 = ntohl(file_crc32);

    return true;
}