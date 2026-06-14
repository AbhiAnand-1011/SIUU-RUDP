#include "protocol.h"
#include "utils.h"

#include <iostream>
#include <vector>
#include <string>
#include <cstring>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc < 4) {
        std::cerr << "usage: sender <receiver_ip> <port> <file_path>\n";
        return 1;
    }

    std::string receiver_ip = argv[1];
    int port = std::stoi(argv[2]);
    std::string filepath = argv[3];

    auto chunks = readFileChunks(filepath, CHUNK_SIZE);
    if (chunks.empty()) {
        std::cerr << "failed to read file\n";
        return 1;
    }

    uint32_t total_chunks = chunks.size();
    std::string filename = filepath.substr(filepath.find_last_of("/\\") + 1);

    std::vector<uint8_t> full;
    for (const auto &c : chunks)
        full.insert(full.end(), c.begin(), c.end());

    uint32_t file_crc = crc32(full);

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = TIMEOUT_MS_DEFAULT * 1000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    inet_pton(AF_INET, receiver_ip.c_str(), &dest.sin_addr);

    bool hello_acked = false;
    int hello_retries = 0;
    while (!hello_acked && hello_retries < MAX_RETRIES) {
        Packet hello = makeHelloPacket(filename, total_chunks, file_crc);
        auto hello_buf = serializePacket(hello);
        sendto(sockfd, hello_buf.data(), hello_buf.size(), 0, (sockaddr *)&dest, sizeof(dest));
        std::cout << "HELLO sent: chunks=" << total_chunks << std::endl;

        std::vector<uint8_t> ack_buf(MAX_PACKET_SIZE);
        sockaddr_in src{};
        socklen_t srclen = sizeof(src);
        ssize_t n = recvfrom(sockfd, ack_buf.data(), ack_buf.size(), 0, (sockaddr *)&src, &srclen);
        
        if (n > 0) {
            ack_buf.resize(n);
            Packet ack_pkt;
            if (deserializePacket(ack_buf, ack_pkt) && ack_pkt.header.type == static_cast<uint8_t>(PacketType::HELLO_ACK)) {
                hello_acked = true;
                std::cout << "HELLO_ACK received\n";
            }
        }
        hello_retries++;
    }

    if (!hello_acked) {
        std::cerr << "Failed to receive HELLO_ACK\n";
        close(sockfd);
        return 1;
    }

    std::vector<bool> acked(total_chunks, false);
    uint32_t acked_count = 0;
    uint32_t base = 0;

    while (acked_count < total_chunks) {
        uint32_t in_flight = 0;
        for (uint32_t i = base; i < total_chunks && in_flight < DEFAULT_WINDOW; i++) {
            if (!acked[i]) {
                uint32_t crc = crc32(chunks[i]);
                Packet pkt = makeDataPacket(i, total_chunks, chunks[i], crc);
                auto buf = serializePacket(pkt);
                sendto(sockfd, buf.data(), buf.size(), 0, (sockaddr *)&dest, sizeof(dest));
                std::cout << "sent chunk " << i << std::endl;
                in_flight++;
            }
        }

        std::vector<uint8_t> ack_buf(MAX_PACKET_SIZE);
        sockaddr_in src{};
        socklen_t srclen = sizeof(src);
        ssize_t n = recvfrom(sockfd, ack_buf.data(), ack_buf.size(), 0, (sockaddr *)&src, &srclen);

        if (n > 0) {
            ack_buf.resize(n);
            Packet ack_pkt;
            if (deserializePacket(ack_buf, ack_pkt) && ack_pkt.header.type == static_cast<uint8_t>(PacketType::ACK)) {
                uint32_t seq = ack_pkt.header.seq;
                if (seq < total_chunks && !acked[seq]) {
                    acked[seq] = true;
                    acked_count++;
                    std::cout << "ACK received for chunk " << seq << std::endl;
                }
            }
        }
        
        while (base < total_chunks && acked[base]) {
            base++;
        }
    }

    bool fin_acked = false;
    int fin_retries = 0;
    while (!fin_acked && fin_retries < MAX_RETRIES) {
        Packet fin = makeFinPacket(file_crc);
        auto fin_buf = serializePacket(fin);
        sendto(sockfd, fin_buf.data(), fin_buf.size(), 0, (sockaddr *)&dest, sizeof(dest));
        std::cout << "FIN sent\n";

        std::vector<uint8_t> ack_buf(MAX_PACKET_SIZE);
        sockaddr_in src{};
        socklen_t srclen = sizeof(src);
        ssize_t n = recvfrom(sockfd, ack_buf.data(), ack_buf.size(), 0, (sockaddr *)&src, &srclen);

        if (n > 0) {
            ack_buf.resize(n);
            Packet ack_pkt;
            if (deserializePacket(ack_buf, ack_pkt) && ack_pkt.header.type == static_cast<uint8_t>(PacketType::FIN_ACK)) {
                fin_acked = true;
                std::cout << "FIN_ACK received\n";
            }
        }
        fin_retries++;
    }

    close(sockfd);
    return 0;
}