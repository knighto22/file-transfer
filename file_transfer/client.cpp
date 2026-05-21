/**
 * 加密文件传输工具 - 客户端
 * 功能：将文件分块加密后通过socket发送给服务端
 * 支持多线程并发发送、断点续传、MD5完整性校验和进度条显示
 * 用法：client.exe [服务端IP]，不传参数默认连接127.0.0.1
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
#include "md5.h"
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

std::mutex sendMutex;

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

void showProgress(int current, int total) {
    std::lock_guard<std::mutex> lock(sendMutex);
    int progress = current * 100 / total;
    int barWidth = 40;
    int filled = barWidth * progress / 100;
    std::cout << "\r[";
    for (int i = 0; i < barWidth; i++)
        std::cout << (i < filled ? '#' : '-');
    std::cout << "] " << progress << "% ("
        << current << "/" << total << ")" << std::flush;
}

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
        showProgress(index + 1, totalChunks);
    }
}

int main(int argc, char* argv[]) {
    std::cout << "当前目录：" << std::filesystem::current_path() << std::endl;

    // 从命令行参数获取IP，默认本机
    std::string serverIp = "127.0.0.1";
    if (argc >= 2) {
        serverIp = argv[1];
    }
    else {
        // 尝试从config.txt读取IP
        std::ifstream config("config.txt");
        if (config) {
            std::getline(config, serverIp);
            serverIp.erase(serverIp.find_last_not_of(" \r\n") + 1); // 去掉多余空格换行
            std::cout << "从config.txt读取IP：" << serverIp << std::endl;
        }
        else {
            std::cout << "未找到config.txt，使用默认IP：" << serverIp << std::endl;
        }
    }

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup失败" << std::endl;
        return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8888);
    inet_pton(AF_INET, serverIp.c_str(), &serverAddr.sin_addr);

    if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) != 0) {
        std::cerr << "连接失败，请检查IP地址和网络" << std::endl;
        return 1;
    }
    std::cout << "已连接服务端：" << serverIp << std::endl;

    // 接收server已完成的块编号（断点续传）
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
        std::cerr << "打开文件失败，请确认 test.txt 在当前目录" << std::endl;
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
    std::cout << std::endl;

    // 计时结束
    auto end = std::chrono::high_resolution_clock::now();
    double seconds = std::chrono::duration<double>(end - start).count();
    double speedMB = (fileSize / 1024.0 / 1024.0) / seconds;

    std::cout << "发送完成！耗时：" << seconds << " 秒" << std::endl;
    std::cout << "传输速度：" << speedMB << " MB/s" << std::endl;

    // 计算原始文件MD5并发送给server验证
    std::string fileMd5 = md5File(filename);
    std::cout << "原始文件 MD5：" << fileMd5 << std::endl;

    int32_t md5Len = htonl(fileMd5.size());
    send(sock, (char*)&md5Len, sizeof(md5Len), 0);
    send(sock, fileMd5.c_str(), fileMd5.size(), 0);

    // 接收server的验证结果
    char resultBuf[4] = {};
    recv(sock, resultBuf, sizeof(resultBuf), 0);
    if (std::string(resultBuf, 2) == "OK") {
        std::cout << "文件完整性验证通过！" << std::endl;
    }
    else {
        std::cout << "警告：文件完整性验证失败！" << std::endl;
    }

    closesocket(sock);
    WSACleanup();
    system("pause");
    return 0;
}