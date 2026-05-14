# 📂 UKVEngine

![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![License](https://img.shields.io/badge/License-Apache%202.0-green.svg)
![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey.svg)

UKVEngine 是一个基于 C++20 从零构建的高性能、极轻量级内存键值存储引擎。它严格遵循 RESP 协议，兼容官方 `redis-cli`，并在底层网络与并发架构上进行了深度的优化与性能调优。

UKVEngine 分为两个部分：

- `ukvd`：UKVEngine 的主体，作为服务端接收并响应来自客户端的请求。
- `ukv`：内置的纯净交互式客户端，与兼容的服务端连接，用户可以在此发送命令。支持 RESP 协议翻译与优雅停机。

## 🚀 核心架构与亮点

* **基于 epoll 的 Reactor 模型：** 直接使用 epoll + 非阻塞 IO 实现事件驱动循环，避免引入第三方网络库。结合轻量线程池处理请求，充分利用多核并行能力。
* **分段锁 LRU：** 将缓存划分为多个 shard，通过哈希路由分发请求，降低全局锁竞争。在高并发场景下，相比单一 LRU 结构，显著减少锁冲突和链表操作开销。
* **热路径低分配：** 手写严苛的 RESP 协议解析状态机。在热点网络输出路径上，应用 `std::to_chars` 配合预留的 `thread_local` 缓冲区，消除 `std::string` 带来的堆内存分配开销。
* **pipeline 极速吞吐：** 支持批量命令解析与处理。在网络写路径中对响应进行聚合，通过减少 `write` 系统调用次数显著提升吞吐量。在 pipeline 模式下可达到百万级 QPS。
* **IPv4/IPv6 双栈支持：** 底层集成 `getaddrinfo` 现代解析机制，无缝支持域名解析与 IPv4/IPv6 混合网络环境。

## 📊 性能基准测试 (Benchmark)

使用标准 `redis-benchmark` 进行压测 (`-n 100000`)。为了验证引擎在不同体系结构下的表现，我们分别在现代高频 CPU 和多核服务器环境下进行了测试。

| 测试场景                 | 环境 A               | 环境 B             |
|:---------------------|:-------------------|:-----------------|
| **单连接极限 (-c 1)**     | ~42,000 QPS        | ~31,000 QPS      |
| **并发甜点区 (-c 100)**   | ~125,000 QPS       | ~84,000 QPS      |
| **极高并发抗压 (-c 1000)** | ~109,000 QPS       | ~78,000 QPS      |
| **管道模式 (-P 16)**     | **~1,330,000 QPS** | **~970,000 QPS** |

- Benchmark 工具: redis-benchmark (loopback 接口)
- 环境 A：Core i5-10200H (4 CPU) @ 4.100 GHz, Arch Linux (Linux 6.18.9-arch1-2)
- 环境 B：Xeon E5-2678v3 (8 vCPU) @ 3.300 GHz, Debian 12 LXC (Linux 6.8.12-15-pve)

> *注：在 Pipeline 模式下，UKVEngine 突破百万 QPS 吞吐。环境 B 的性能下降主要来源于容器网络路径和 CPU 调度带来的额外开销。*

## ⚙️ 性能优化过程

在开发过程中，通过 redis-benchmark 对系统进行持续压测与分析：

- 初始版本在 pipeline 模式下性能异常（QPS < 40k）
- 通过分析发现每条命令单独 write，导致 syscall 开销过高
- 改为批量拼接响应并统一 write 后：
    - Pipeline QPS 提升至 1M+

| 迭代 | 问题 | 修复 | 效果 |
|---|---|---|---|
| v1 | Pipeline 模式下 QPS 异常低（~38k） | 实现 out_buffer，聚合响应后统一 write | Pipeline QPS 提升至 1.3M，涨幅 34x |
| v2 | 高并发下（c=1000）吞吐退化明显 | 引入分段锁 LRU，按 key hash 路由到独立 shard | c=1000 吞吐提升约 15%，锁竞争显著降低 |

该过程帮助定位了系统瓶颈主要来自 syscall，而非协议解析或数据结构。

## ⌨️ 支持的命令

- `SET`：`SET <key> <value>`，新增或修改 `key` 的值为 `value`。
- `GET`：`GET <key>`，返回键 `key` 对应的值 `value`，否则返回空结果。
- `DEL`：`DEL <key> [key1] [key2] [...]`，删除给定的键，返回成功删除的键值对个数。

## 🌐 Roadmap

- [x] **AOF 持久化**：支持 Append-Only File，实现重启后数据恢复
- [ ] **TTL 与键过期**：实现 `EXPIRE` / `TTL`，支持惰性删除与后台定期清理
- [ ] **更完整的命令支持**：`EXISTS`、`INCR` / `DECR`、`KEYS` 等
- [ ] **Multi-Reactor 模型**：将当前 Reactor + 线程池架构改造为多线程独立 event loop，进一步降低高并发延迟

---

## 💻 客户端使用 (ukv)

项目内置了一个纯净的交互式客户端，支持完整的 RESP 协议翻译与优雅停机。

```bash
# 连接至本地服务器
./ukv -h 127.0.0.1 -p 8586

# 支持 IPv6
./ukv -h ::1 -p 8586
```

**交互示例：**
```bash
ukv> SET project_name UKVEngine
OK
ukv> SET "engine name" "UKVEngine"
OK
ukv> GET "engine name"
"UKVEngine"
ukv> DEL project_name "engine name"
(integer) 2
```

---

## 🛠️ 构建与运行

项目依赖较少，需要支持 C++20 的编译器以及 CMake。

```bash
# 1. 克隆代码
git clone https://github.com/Siaospeed/UKVEngine.git
cd UKVEngine

# 2. 编译构建
mkdir build && cd build
cmake ..
make -j$(nproc)

# 3. 启动服务端 (默认运行在 8586 端口)
./ukvd -p 8586

# 4. 启动客户端 (默认连接 127.0.0.1:8586)
./ukv -h localhost -p 8586
```

## 🤝 贡献与协议

本项目采用 Apache License 2.0 协议开源。

你可以自由使用、修改、分发本项目，但必须：

- 保留原作者署名 (Siaospeed)
- 在引用时注明原项目来源

`Copyright 2026 Siaospeed`

欢迎提交 Issue 或 Pull Request。
