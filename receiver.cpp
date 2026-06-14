#include "protocol.h"
#include "utils.h"

#include <iostream>
#include <vector>
#include <string>
#include <unordered_set>
#include <cstring>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc < 3) {
        std::cerr << "usage: receiver <port> <outdir>\n";
        return 1;
    }

    int port = std::stoi(argv[1]);
    std::string outdir = argv[2];

    ensureDir(outdir);

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sockfd, (sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sockfd);
        return 1;
    }

    std::cout << "receiver listening on port " << port << std::endl;

    uint32_t total_chunks = 0;
    std::string filename;
    uint32_t file_crc = 0;
    std::unordered_set<uint32_t> received;

    while (true) {
        std::vector<uint8_t> buf(MAX_PACKET_SIZE);
        sockaddr_in src{};
        socklen_t srclen = sizeof(src);

        ssize_t n = recvfrom(sockfd, buf.data(), buf.size(), 0,
                             (sockaddr *)&src, &srclen);
        if (n <= 0) continue;

        buf.resize(n);

        Packet pkt;
        if (!deserializePacket(buf, pkt)) continue;

        PacketType type = static_cast<PacketType>(pkt.header.type);

        if (type == PacketType::HELLO) {
            if (!parseHelloPayload(pkt.payload, filename, total_chunks, file_crc))
                continue;

            std::cout << "HELLO received: file=" << filename
                      << " chunks=" << total_chunks << std::endl;

            Packet ack = makeHelloAckPacket({});
            auto out = serializePacket(ack);
            sendto(sockfd, out.data(), out.size(), 0,
                   (sockaddr *)&src, srclen);
        }

        else if (type == PacketType::DATA) {
            uint32_t seq = pkt.header.seq;

            if (crc32(pkt.payload) == pkt.header.checksum) {

                if (received.find(seq) == received.end()) {
                    writeChunkToTemp(outdir, seq, pkt.payload);
                    received.insert(seq);
                    std::cout << "received chunk " << seq << std::endl;
                }

                Packet ack = makeAckPacket(seq, {}); 
                auto out = serializePacket(ack);
                sendto(sockfd, out.data(), out.size(), 0,
                       (sockaddr *)&src, srclen);
            }

            if (total_chunks > 0 && received.size() == total_chunks) {
                std::cout << "all chunks received\n";
                assembleChunksFromDir(outdir, outdir + "/" + filename, total_chunks);
                std::cout << "file assembled\n";
                break;
            }
        }
        
        else if (type == PacketType::FIN) {
            Packet fin_ack = makeFinAckPacket();
            auto out = serializePacket(fin_ack);
            sendto(sockfd, out.data(), out.size(), 0, (sockaddr *)&src, srclen);
            std::cout << "FIN received, sent FIN_ACK\n";
        }
    }

    close(sockfd);
    return 0;
}