\# 加密文件传输工具



基于 C++ 实现的高性能加密文件传输系统，支持多线程并发传输、AES-256 加密和断点续传。



\## 技术栈



\- \*\*网络通信\*\*：Windows Socket (TCP)

\- \*\*序列化协议\*\*：Protocol Buffers (protobuf)

\- \*\*加密算法\*\*：AES-256-CBC (OpenSSL)

\- \*\*并发模型\*\*：C++17 多线程 + Mutex

\- \*\*开发环境\*\*：Visual Studio 2022 / C++17



\## 核心功能



\- 文件分块传输：将文件切分为 1KB 块逐块传输，支持大文件

\- AES-256 加密：传输数据全程加密，防止中间人截获

\- 多线程并发：4 线程并发发送，传输速度约 1.5 MB/s

\- 断点续传：记录已传输块，网络中断后重连可从断点继续



\## 项目结构



file\_transfer/

├── server/

│   └── server.cpp       # 服务端：接收、解密、拼装文件

├── client/

│   └── client.cpp       # 客户端：分块、加密、多线程发送

├── crypto.h             # AES 加密/解密封装

├── transfer.proto       # protobuf 协议定义

├── transfer.pb.h        # protobuf 生成文件

└── transfer.pb.cc       # protobuf 生成文件





\## 运行方式



\*\*依赖安装（使用 vcpkg）\*\*

```bash

vcpkg install protobuf:x64-windows

vcpkg install openssl:x64-windows

```



\*\*编译\*\*



用 Visual Studio 2022 打开 `file\_transfer.sln`，分别生成 server 和 client 项目。



\*\*运行\*\*



先启动服务端：server.exe

再启动客户端（确保 test.txt 在同目录下）：client.exe



\## 设计要点



\*\*粘包处理\*\*：TCP 是流式协议，每条消息前附加 4 字节长度头，接收端按长度精确读取，避免粘包问题。



\*\*协议设计\*\*：使用 protobuf 定义传输协议，相比 JSON 体积减小约 3-5 倍，解析速度更快。



\*\*线程安全\*\*：多线程发送时使用 Mutex 保护 socket 写操作，避免数据竞争。



\*\*断点续传\*\*：服务端每收到一块立即写入 progress.txt，重连后将已完成块编号发给客户端，客户端跳过已完成块。

