/**
 * 加密文件传输工具 - 服务端
 * 功能：接收客户端发送的加密文件块，解密后拼装还原文件
 * 支持断点续传：记录已接收的块，重连后从断点继续
 */
#include <iostream>
#include <fstream>
#include <map>
#include <set>
#include <winsock2.h>
#include "transfer.pb.h"
#include "crypto.h"
#pragma comment(lib, "ws2_32.lib")

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

void sendMessage(SOCKET sock, const google::protobuf::Message& msg) {
    std::string data;
    msg.SerializeToString(&data);
    int32_t len = htonl(data.size());
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

// 读取已完成的块编号
std::set<int32_t> loadProgress(const std::string& filename) {
    std::set<int32_t> done;
    std::ifstream f(filename);
    int32_t idx;
    while (f >> idx) done.insert(idx);
    return done;
}

// 保存已完成的块编号
void saveProgress(const std::string& filename, const std::set<int32_t>& done) {
    std::ofstream f(filename);
    for (int32_t idx : done) f << idx << "\n";
}

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup失败" << std::endl;
        return 1;
    }

    SOCKET serverSock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8888);
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    bind(serverSock, (sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(serverSock, 5);
    std::cout << "服务端启动，等待连接..." << std::endl;

    SOCKET clientSock = accept(serverSock, nullptr, nullptr);
    std::cout << "客户端已连接！" << std::endl;

    // 加载已有进度
    std::string progressFile = "progress.txt";
    std::set<int32_t> doneChunks = loadProgress(progressFile);
    std::cout << "已完成块数：" << doneChunks.size() << std::endl;

    // 把已完成的块编号发给client
    std::string progressData;
    for (int32_t idx : doneChunks) {
        progressData += std::to_string(idx) + "\n";
    }
    int32_t pLen = htonl(progressData.size());
    send(clientSock, (char*)&pLen, sizeof(pLen), 0);
    if (!progressData.empty()) {
        send(clientSock, progressData.c_str(), progressData.size(), 0);
    }

    std::map<int32_t, std::string> chunks;
    int32_t totalChunks = 0;
    std::string filename;
    int64_t fileSize = 0;

    while (true) {
        std::string data = recvMessage(clientSock);
        if (data.empty()) break;

        FileChunk chunk;
        chunk.ParseFromString(data);

        filename = chunk.filename();
        fileSize = chunk.total_size();
        totalChunks = chunk.total_chunks();

        std::string decrypted = aesDecrypt(chunk.data(), key, iv);
        chunks[chunk.chunk_index()] = decrypted;
        doneChunks.insert(chunk.chunk_index());
        saveProgress(progressFile, doneChunks);

        std::cout << "收到块 " << doneChunks.size()
            << "/" << totalChunks << std::endl;

        if ((int32_t)doneChunks.size() == totalChunks) break;
    }

    // 拼装文件
    std::string outFilename = "received_" + filename;
    std::ofstream outFile(outFilename, std::ios::binary);
    for (int32_t i = 0; i < totalChunks; i++) {
        outFile.write(chunks[i].c_str(), chunks[i].size());
    }
    outFile.close();

    // 删除进度文件
    std::remove(progressFile.c_str());
    std::cout << "文件接收完成！保存为：" << outFilename << std::endl;

    closesocket(clientSock);
    closesocket(serverSock);
    WSACleanup();
    return 0;
}