/**
 * 加密文件传输工具 - 客户端
 * 功能：将文件分块加密后通过socket发送给服务端
 * 支持多线程并发发送和断点续传
 */
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <iostream>
#include <sstream>
#include <set>
#include <fstream>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <filesystem>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "transfer.pb.h"
#include "crypto.h"
#pragma comment(lib, "ws2_32.lib")

const int CHUNK_SIZE = 1024;

unsigned char key[32] = {
    '1','2','3','4','5','6','7','8',
    '9','0','1','2','3','4','5','6',
    '7','8','9','0','1','2','3','4',
    '5','6','7','8','9','0','1','2'
};
unsigned char iv[16] = {
    '1','2','3','4','5','6','7','8',
    '9','0','1','2','3','4','5','6'
};

std::mutex sendMutex; // 保护socket发送

void sendMessage(SOCKET sock, const google::protobuf::Message& msg) {
    std::string data;
    msg.SerializeToString(&data);
    int32_t len = htonl(data.size());
    std::lock_guard<std::mutex> lock(sendMutex);
    send(sock, (char*)&len, sizeof(len), 0);
    send(sock, data.c_str(), data.size(), 0);
}

std::string recvMessage(SOCKET sock) {
    int32_t len = 0;
    recv(sock, (char*)&len, sizeof(len), 0);
    len = ntohl(len);
    std::string buf(len, 0);
    recv(sock, &buf[0], len, 0);
    return buf;
}

// 每个线程负责发送一部分块
void sendChunks(SOCKET sock,
    const std::vector<std::pair<int32_t, std::string>>& chunks,
    int32_t totalChunks,
    const std::string& filename,
    int64_t fileSize) {
    for (auto& [index, data] : chunks) {
        std::string encrypted = aesEncrypt(data, key, iv);

        FileChunk chunk;
        chunk.set_filename(filename);
        chunk.set_total_size(fileSize);
        chunk.set_chunk_index(index);
        chunk.set_total_chunks(totalChunks);
        chunk.set_data(encrypted);

        sendMessage(sock, chunk);
        std::cout << "发送块 " << index + 1 << "/" << totalChunks << std::endl;
    }
}

int main() {
    std::cout << "当前目录：" << std::filesystem::current_path() << std::endl;

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup失败" << std::endl;
        return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8888);
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);
    connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr));
    std::cout << "已连接服务端" << std::endl;

    // 接收server已完成的块编号
    std::set<int32_t> doneChunks;
    int32_t pLen = 0;
    recv(sock, (char*)&pLen, sizeof(pLen), 0);
    pLen = ntohl(pLen);
    if (pLen > 0) {
        std::string progressData(pLen, 0);
        recv(sock, &progressData[0], pLen, 0);
        std::istringstream iss(progressData);
        int32_t idx;
        while (iss >> idx) doneChunks.insert(idx);
        std::cout << "断点续传，跳过已完成块：" << doneChunks.size() << std::endl;
    }

    // 读取文件
    std::string filename = "test.txt";
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "打开文件失败" << std::endl;
        return 1;
    }

    file.seekg(0, std::ios::end);
    int64_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    int32_t totalChunks = (fileSize + CHUNK_SIZE - 1) / CHUNK_SIZE;
    std::cout << "文件大小：" << fileSize << " 字节，共 "
        << totalChunks << " 块" << std::endl;

    // 读取所有块到内存
    std::vector<std::pair<int32_t, std::string>> allChunks;
    std::vector<char> buf(CHUNK_SIZE);
    for (int32_t i = 0; i < totalChunks; i++) {
        file.read(buf.data(), CHUNK_SIZE);
        int64_t bytesRead = file.gcount();
        allChunks.push_back({ i, std::string(buf.data(), bytesRead) });
    }
    file.close();

    // 分配给多个线程，跳过已完成的块
    int32_t threadCount = 4;
    std::vector<std::thread> threads;
    std::vector<std::vector<std::pair<int32_t, std::string>>> threadChunks(threadCount);

    for (int32_t i = 0; i < totalChunks; i++) {
        if (doneChunks.count(i) == 0) {
            threadChunks[i % threadCount].push_back(allChunks[i]);
        }
    }

    // 计时开始
    auto start = std::chrono::high_resolution_clock::now();

    // 启动线程
    for (int32_t i = 0; i < threadCount; i++) {
        if (!threadChunks[i].empty()) {
            threads.emplace_back(sendChunks, sock,
                std::ref(threadChunks[i]),
                totalChunks, filename, fileSize);
        }
    }

    // 等待所有线程完成
    for (auto& t : threads) t.join();

    // 计时结束
    auto end = std::chrono::high_resolution_clock::now();
    double seconds = std::chrono::duration<double>(end - start).count();
    double speedMB = (fileSize / 1024.0 / 1024.0) / seconds;

    std::cout << "发送完成！耗时：" << seconds << " 秒" << std::endl;
    std::cout << "传输速度：" << speedMB << " MB/s" << std::endl;

    closesocket(sock);
    WSACleanup();
    system("pause");
    return 0;
}