# Changelog

## [1.0.0] - 2026-05-31

First stable release.

### Features
- Multi-segment download engine (IOCP + WinHTTP, up to 16 connections)
- Smart chunk strategy: BDP-aware adaptive chunking
- HTTP/2 support (Windows 10 1607+)
- Connection pool: reuse connections for same-host batch downloads
- Resume from .arisa control file
- Auto retry with exponential backoff
- HEAD probe: auto-detect file size and Range support
- CLI: rs <url> -o -n -c --rpc-port
- Full aria2 JSON-RPC 2.0 compatibility (29 methods)
- Motrix GUI verified as aria2 replacement
- Zero external dependencies (clean-room implementation)

### Known Limitations
- Windows only (Linux/macOS planned)
- No BitTorrent / Metalink support
- No speed limiter
- No proxy authentication

All notable changes to Arisa Engine will be documented in this file.

Format based on [Keep a Changelog](https://keepachangelog.com/).

## [0.7.1] - 2026-05-30

### Fixed
- Chunk count off-by-one: 1-connection download was creating 0 chunks due to residual while loop
- Smart chunking now correctly returns 1 chunk for single-connection mode

## [0.7.0] - 2026-05-30

### Added
- HTTP/2 support via WinHTTP (Windows 10 1607+)
- Connection pool: reuse TCP+TLS connections for same-host batch downloads
- Smart chunk strategy: BDP-aware adaptive chunking based on file size and network conditions
- chunk_strategy.h: small files (<1MB) single connection, large files adaptive
- connection_pool.h/cpp: thread-safe pooled connection management

### Changed
- Chunk allocation now respects 12MB per-piece heuristic instead of fixed count
- User-Agent updated to Arisa/0.7

## [0.6.0] - 2026-05-30

### Added
- Complete aria2 JSON-RPC 2.0 compatibility (29 methods)
- system.multicall, system.listMethods, system.listNotifications
- ria2.tellActive, ria2.tellWaiting, ria2.tellStopped with paging
- ria2.getFiles, ria2.getUris, ria2.getServers
- ria2.getOption, ria2.changeOption, ria2.changePosition, ria2.changeUri
- ria2.purgeDownloadResult, ria2.removeDownloadResult
- ria2.saveSession, ria2.getGlobalOption, ria2.changeGlobalOption
- ria2.pauseAll, ria2.forcePauseAll
- Motrix GUI fully verified as aria2 replacement

## [0.5.0] - 2026-05-30

### Added
- aria2-compatible JSON-RPC 2.0 server (--rpc-port 6800)
- Custom header-only JSON parser (pc/json.h)
- ria2.getVersion, ria2.addUri, ria2.remove, ria2.tellStatus, ria2.getGlobalStat, ria2.shutdown
- Motrix download verification passed

## [0.4.0] - 2026-05-29

### Added
- IOCP async I/O engine
- WinHTTP multi-segment download (8 parallel chunks)
- Resume from .arisa control file
- Auto retry with exponential backoff (2s, 4s, 6s...)
- HEAD probe for file size + Range support detection
- CLI: rs <url> -o -n -c
- Clean-room implementation, no external dependencies

[0.7.1]: https://github.com/ama-hara/arisa/compare/v0.7.0...v0.7.1
[0.7.0]: https://github.com/ama-hara/arisa/compare/v0.6.0...v0.7.0
[0.6.0]: https://github.com/ama-hara/arisa/compare/v0.5.0...v0.6.0
[0.5.0]: https://github.com/ama-hara/arisa/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/ama-hara/arisa/releases/tag/v0.4.0