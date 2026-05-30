# Arisa Download Engine

> "...才不是特意帮你的"

高性能多线程下载引擎，基于 Windows 原生 API (IOCP + WinHTTP) 实现，完全 clean-room 开发，无外部依赖。

命令行工具 `ars`，兼容 aria2 JSON-RPC 协议，可对接 Motrix、aria2-ng 等前端。

## 特性

- **多线程分片下载** — 8 线程并行，实测 33+ MB/s
- **HTTP/HTTPS** — WinHTTP 原生支持，无需 OpenSSL
- **断点续传** — `.arisa` 控制文件记录分片进度，中断后自动恢复
- **自动重试** — 指数退避 (2s, 4s, 6s...)，最多重试 3 次
- **智能分片** — HEAD 探测文件大小 + Range 支持检测，自动选择单连接/多分片策略
- **aria2 RPC 兼容** — JSON-RPC 2.0 服务器，端口 6800
- **IOCP 异步 I/O** — Windows 最强原生异步模型

## 快速开始

### 构建

```powershell
# 需要: Windows 11, Visual Studio Build Tools 2026 (MSVC 19.51+), CMake 3.25+
cmake -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Debug
```

### 使用

```shell
# 基本下载
ars https://example.com/file.zip

# 指定输出目录和并发数
ars https://example.com/file.zip -o D:\Downloads -c 8

# 启用 aria2 RPC 服务器（可对接 Motrix / aria2-ng）
ars https://example.com/file.zip --rpc-port 6800
```

### 命令行参数

```
ars <url> [options]

  -o, --output <dir>      输出目录（默认: .）
  -n, --name <filename>   文件名（默认: 从 URL 自动推断）
  -c, --connections <n>   并发连接数/分片数（默认: 8）
  --rpc-port <port>       启用 aria2 兼容 JSON-RPC 服务器
```

### 断点续传

下载中断后，重新运行同一命令即可自动从断点恢复。引擎会读取 `.arisa` 控制文件，跳过已完成的分片。下载成功后控制文件自动删除。

## RPC API

使用 `--rpc-port 6800` 启动后，连接 `http://127.0.0.1:6800/jsonrpc`。

支持的 aria2 方法：

| 方法 | 说明 |
|---|---|
| `aria2.getVersion` | 获取版本和特性 |
| `aria2.addUri` | 添加下载任务（支持 `dir`, `out`, `split` 选项） |
| `aria2.remove` / `forceRemove` | 移除任务 |
| `aria2.tellStatus` | 查询任务状态（含进度、速度、文件信息） |
| `aria2.getGlobalStat` | 全局统计 |
| `aria2.shutdown` / `forceShutdown` | 关闭引擎 |

示例：

```json
{
  "jsonrpc": "2.0",
  "id": "1",
  "method": "aria2.addUri",
  "params": [
    ["https://example.com/file.zip"],
    {"dir": "./downloads", "split": 8}
  ]
}
```

## 技术架构

```
┌─────────────────────────────────┐
│         CLI / RPC Server        │
├─────────────────────────────────┤
│        Task Scheduler           │
│   队列管理 / 并发控制 / 重试     │
├─────────────────────────────────┤
│        HTTP Client (WinHTTP)    │
│   probe / download / range      │
├─────────────────────────────────┤
│     IOCP Async I/O Engine       │
├─────────────────────────────────┤
│     Windows Platform Layer      │
│   WinSock / WinHTTP / IOCP      │
└─────────────────────────────────┘
```

## 与 aria2 的区别

| | Arisa Engine | aria2 |
|---|---|---|
| 异步 I/O | IOCP (内核级) | select/poll (用户态) |
| HTTP/2 | 计划支持 | 不支持 |
| 协议实现 | WinHTTP (原生) | 自研 libcurl-like |
| TLS | Windows 原生 | OpenSSL/GnuTLS |
| C++ 标准 | C++23 | C++98 |
| BT 下载 | 不支持 | 支持 |

## 构建要求

- Windows 10/11 (x64)
- Visual Studio Build Tools 2022+ (MSVC 19.51+)
- CMake 3.25+
- C++23 支持

## 项目结构

```
arisa/
├── CMakeLists.txt
├── build.ps1
├── README.md
├── LICENSE
├── .gitignore
└── src/
    ├── main.cpp
    ├── include/arisa/
    │   ├── core/       (config, types, engine, scheduler, resume)
    │   ├── io/         (async_io, iocp_engine)
    │   ├── net/        (http_client)
    │   └── rpc/        (json, aria2_methods, rpc_server)
    ├── core/
    ├── io/
    ├── net/
    └── rpc/
```

## 许可证

MIT License

## 名称来源

项目以 BanG Dream! 中 Poppin'Party 键盘手 **市ヶ谷有咲** (Ichigaya Arisa) 命名。

> "没我你可怎么办"

## 本项目由 Xiaomi Mimo-V2.5-Pro 辅助完成