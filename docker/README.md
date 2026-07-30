# Docker 跨架构示例

本目录集中管理 Windows AMD64 构建、ARM64 模拟测试、Harbor 推送和
ARM64 服务器部署所需的 Docker 配置。示例源码统一放在仓库根目录的
`workspace/` 中。

## 示例索引

| 示例 | 源码 | 构建配置 | 使用指南 |
| --- | --- | --- | --- |
| 标准 C++ Hello World | `workspace/helloworld/` | `docker/helloworld/` | [联网流程](helloworld/README.md) / [无互联网局域网流程](helloworld/OFFLINE.md) |
| Qt 5.15 + qmake + Qt Core | `workspace/qt5core/` | `docker/qt5core/` | [Qt 5.15 迁移指南](qt5core/README.md) |

## 最短本地调试命令

标准 C++：

```powershell
Set-Location C:\home\cxx\uestcradar\docker\helloworld
docker compose up --build --force-recreate
```

Qt 5.15：

```powershell
Set-Location C:\home\cxx\uestcradar\docker\qt5core
docker compose up --build --force-recreate
```

两个 Compose 项目彼此独立。停止并清理当前示例：

```powershell
docker compose down
```
