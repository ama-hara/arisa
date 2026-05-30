# Arisa Download Engine

> "...才不是特意帮你的"

超轻量化高性能多线程下载引擎，基于原生 API 构建，零外部依赖，完全净室实现了aria2的大部分功能。

命令行工具 `ars`，完整兼容 aria2 JSON-RPC 协议，可无缝替换 aria2 作为 Motrix、aria2-ng 等前端的后端引擎。

## 自卖自夸

| | Arisa Engine | aria2 |
|---|---|---|
| **异步模型** | IOCP（内核级完成端口） | select/poll（用户态轮询） |
| **协议栈** | WinHTTP 原生（系统级 TLS） | 自研 HTTP + OpenSSL |
| **HTTP/2** | 支持（WinHTTP 透传） | 不支持 |
| **C++ 标准** | C++23 | C++98 |
| **外部依赖** | 零 | zlib, OpenSSL, c-ares, libssh2... |
| **BT 下载** | 不支持 | 支持 |

简而言之：更现代、更轻量、更专注 HTTP/HTTPS 场景。

## 特性

### 下载引擎

- **多线程分片下载** — 最多 16 路并行，实测大文件 50+ MB/s
- **智能分片策略** — 基于带宽延迟积（BDP）自适应分片：小文件单连接避免握手开销，大文件按网络状况动态分配
- **HTTP/2 多路复用** — 单连接多流，减少握手延迟（Windows 10 1607+）
- **连接池** — 同主机批量下载复用 TCP+TLS 连接，百个文件只需一次握手
- **断点续传** — `.arisa` 控制文件记录每片进度，中断后自动恢复已完成分片
- **自动重试** — 指数退避（2s, 4s, 6s...），最多 3 次
- **HEAD 探测** — 自动检测文件大小和 Range 支持，决定单连接/多分片策略
- **文件预分配** — 多分片模式下预先分配磁盘空间，避免碎片

### aria2 RPC 兼容

完整实现 aria2 JSON-RPC 2.0 协议，29 个方法全覆盖：

```
下载管理     addUri, addTorrent, addMetalink
任务控制     remove, forceRemove, pause, forcePause
             pauseAll, forcePauseAll, unpause, unpauseAll
状态查询     tellStatus, tellActive, tellWaiting, tellStopped
文件信息     getFiles, getUris, getServers
选项管理     getOption, changeOption, getGlobalOption, changeGlobalOption
结果管理     purgeDownloadResult, removeDownloadResult
队列管理     changePosition, changeUri
会话管理     saveSession, shutdown, forceShutdown
系统         system.multicall, system.listMethods, system.listNotifications
```

已验证前端：**Motrix** — 直接替换其内置 aria2，下载功能完全正常。

### 其他

- **零外部依赖** — 纯 Windows API (WinHTTP + WinSock + IOCP)，无需安装任何运行时
- **自研 JSON 解析器** — header-only，支持完整的 JSON 规范
- **三语 CLI** — 支持中文 / 日文 / 英文（实验性）

## 快速开始


    我操，忘了写了  


### 系统要求

- Windows 10 x64 (1607+) / Windows 11
- Visual Studio Build Tools 2022+ (MSVC 19.51+)
- CMake 3.25+

### 构建

```powershell
# 一键构建
.\build.ps1

# 或手动
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

构建产物位于 `build\Release\ars.exe`。

### 下载文件

```powershell
# 基本用法
ars https://example.com/file.zip

# 指定输出目录和并发数
ars https://example.com/file.zip -o D:\Downloads -c 8

# 指定文件名
ars https://example.com/file.zip -o D:\Downloads -n myfile.zip
```

### 断点续传

下载中断后重新运行同一命令，引擎自动读取 `.arisa` 控制文件，跳过已完成分片：

```example
# 第一次（中断了）
ars https://example.com/large-file.iso -c 8
# [ars] 3200.00/8147.82 MB (39%) | Ctrl+C 中断

# 第二次（自动续传）
ars https://example.com/large-file.iso -c 8
# [Scheduler] Task 1 RESUMING from .arisa
# [Scheduler]   already: 3200.00 MB
# [ars] 8147.82/8147.82 MB (100%) | 48.7 MB/s
```

### 作为 aria2 后端

```powershell
# 启动 RPC 服务器
ars --rpc-port 6800
```

然后在 Motrix / aria2-ng 中配置 RPC 地址：`http://127.0.0.1:6800/jsonrpc`

直接替换 Motrix 内置的 aria2 二进制文件同样可行。

### 命令行参数

```
ars <url> [options]

  -o, --output <dir>      输出目录（默认: 当前目录）
  -n, --name <filename>   文件名（默认: 从 URL 自动推断）
  -c, --connections <n>   并发连接数/分片数（默认: 8，最大: 16）
  --rpc-port <port>       启用 aria2 兼容 JSON-RPC 服务器
```

## 架构

```
┌───────────────────────────────────────────────┐
│              CLI / aria2 RPC Server            │
│          ars 命令行 / JSON-RPC 2.0             │
├───────────────────────────────────────────────┤
│              Task Scheduler                    │
│    队列管理 / 并发控制 / 智能分片 / 自动重试     │
├───────────────────────────────────────────────┤
│         HTTP Client (WinHTTP)                  │
│   probe / download_file / download_range       │
├───────────────────────────────────────────────┤
│       Connection Pool + HTTP/2                 │
│   同主机连接复用 / 多路复用                      │
├───────────────────────────────────────────────┤
│        IOCP Async I/O Engine                   │
│   内核级完成端口 / 异步读写                       │
├───────────────────────────────────────────────┤
│         Windows Platform Layer                 │
│   WinSock2 / WinHTTP / IOCP / Kernel32         │
└───────────────────────────────────────────────┘
```

## 项目结构

```
arisa/
├── CMakeLists.txt
├── build.ps1                 # 一键构建脚本
├── README.md
├── LICENSE                   # MIT
├── CHANGELOG.md
├── .gitignore
├── .gitattributes
└── src/
    ├── main.cpp              # CLI 入口 + RPC 服务器启动
    ├── include/arisa/
    │   ├── core/
    │   │   ├── config.h          # 全局配置常量
    │   │   ├── types.h           # 公共类型定义
    │   │   ├── engine.h          # 引擎主类
    │   │   ├── scheduler.h       # 任务调度器
    │   │   └── resume_manager.h  # 断点续传控制文件
    │   ├── io/
    │   │   ├── async_io.h        # 异步 I/O 抽象接口
    │   │   └── iocp_engine.h     # IOCP 实现
    │   ├── net/
    │   │   ├── http_client.h     # HTTP 下载
    │   │   ├── connection_pool.h # 连接池
    │   │   └── chunk_strategy.h  # 智能分片策略
    │   └── rpc/
    │       ├── json.h            # 自研 JSON 解析器
    │       ├── aria2_methods.h   # aria2 方法分发器
    │       └── rpc_server.h      # JSON-RPC 服务器
    ├── core/
    │   ├── engine.cpp
    │   └── scheduler.cpp
    ├── io/
    │   └── iocp_engine.cpp
    ├── net/
    │   └── http_client.cpp
    │   └── connection_pool.cpp
    └── rpc/
        └── rpc_server.cpp
```

## 版本历史

| 版本 | 日期 | 亮点 |
|---|---|---|
| **v1.0.0** | 2026-05 | 首个正式版，aria2 RPC 29 方法全覆盖 |
| v0.7.0 | 2026-05 | HTTP/2 + 连接池 + 智能分片策略 |
| v0.6.0 | 2026-05 | 初步 aria2 RPC 兼容，Motrix 验证通过 |
| v0.5.0 | 2026-05 | JSON-RPC 服务器，aria2 基础方法 |
| v0.4.0 | 2026-05 | IOCP 引擎 + 多线程下载 + 断点续传 |

详见 [CHANGELOG.md](CHANGELOG.md)。

## 开发路线

### 近期

- [ ] aria2 RPC WebSocket 传输
- [ ] macOS 支持（kqueue）
- [ ] Kasumi — Qt 图形前端
- [ ] Linux 支持（epoll/io_uring 替换 IOCP）
- [ ] 下载速度限制
- [ ] Proxy 认证支持

### 中期


- [ ] HTTP Digest 认证


### 远期

- [ ] Metalink 支持
- [ ] aria2 RPC 完整兼容测试套件

## 许可证

[MIT License](LICENSE)

## 名称

项目以 BanG Dream! 中 Poppin'Party 的键盘手 **市ヶ谷有咲** (Ichigaya Arisa) 命名。

> "才、才没有喜欢你！"

## 本项目的大部分代码为 Xiaomi MiMo-V2.5-Pro AI生成