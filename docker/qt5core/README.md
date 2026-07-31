# Qt 5.15 + qmake + Qt Core ARM64 Docker 迁移示例

本示例独立于仓库根目录的普通 Hello World，用于验证客户 qmake/Qt 5.15 程序的容器迁移路径。

示例具备以下特性：

- 使用 qmake，不要求先转换为 CMake。
- 使用 Qt 5.15 的 `QCoreApplication` 和 `QThread`。
- 不依赖 Qt GUI、Qt Widgets、X11 或显示器。
- 在 Windows AMD64 Docker Desktop 上构建 `linux/arm64` 镜像。
- 支持 Windows 本地模拟运行、Harbor 部署和无互联网局域网部署。
- 正确处理 `SIGINT` 和 `SIGTERM`，支持 `Ctrl+C` 与 `docker stop`。

## 1. 文件结构

```text
uestcradar/
├── workspace/
│   └── examples/
│       └── qt5core/
│           └── src/
│               ├── main.cpp
│               └── qt5core_thread_example.pro
└── docker/
    └── qt5core/
        ├── compose.yaml
        ├── Dockerfile
        └── README.md
```

## 2. qmake 的纯 Qt Core 配置

示例 `.pro` 的关键内容：

```qmake
QT += core
QT -= gui

CONFIG += console c++17
CONFIG -= app_bundle
```

`QT -= gui` 很重要。qmake 默认会加入 Qt Core 和 Qt GUI；显式删除 GUI 后，程序不会链接 Qt GUI 或 Widgets。

客户程序如果仍然包含 `QApplication`、`QWidget`、`QMainWindow` 或 `.ui` 文件，不能直接删除 GUI 依赖。应先添加基于 `QCoreApplication` 的 headless 入口，并在 `.pro` 中按 `CONFIG+=headless` 条件选择源码。

## 3. Qt 版本

当前 Ubuntu 24.04 ARM64 软件源实际提供：

```text
Qt 5.15.13
```

它属于 Qt 5.15 系列。客户如果使用定制 Qt 5.15 SDK、特殊编译选项或私有模块，建议把 Docker 构建镜像替换为客户同源 Qt 工具链，避免发行版 Qt 与客户 Qt 之间的 ABI、插件或配置差异。

## 4. Windows 一条命令构建并调试

进入 Qt 5 Core Docker 目录：

```powershell
Set-Location C:\home\cxx\uestcradar\docker\qt5core

docker compose `
  up --build --force-recreate qt5core-example
```

预期输出：

```text
Qt version: 5.15.13
QtCore heartbeat #1 at ...
QtCore heartbeat #2 at ...
QtCore heartbeat #3 at ...
```

按 `Ctrl+C` 后，程序应输出：

```text
Stop signal received
QtCore worker thread stopped
```

清理测试容器：

```powershell
docker compose down
```

## 5. 后台调试

启动：

```powershell
docker compose `
  up -d --build --force-recreate qt5core-example
```

持续查看日志：

```powershell
docker compose `
  logs -f qt5core-example
```

检查状态：

```powershell
docker compose ps
```

停止：

```powershell
docker compose stop
```

停止日志中应包含线程正常结束信息。

## 6. 检查镜像和运行依赖

```powershell
$image = "registry.chengyistudio.com/cxx/qt5core-thread-example:0.1.0"

docker image inspect $image `
  --format "architecture={{.Architecture}} os={{.Os}} entrypoint={{json .Config.Entrypoint}}"
```

预期：

```text
architecture=arm64 os=linux entrypoint=["/app/qt5core_thread_example"]
```

检查运行时 Qt 依赖：

```powershell
docker run --rm `
  --platform linux/arm64 `
  --entrypoint ldd `
  $image `
  /app/qt5core_thread_example
```

输出不能包含：

```text
not found
```

## 7. 推送 Harbor

先设置一个新的版本号：

```powershell
$image = "registry.chengyistudio.com/cxx/qt5core-thread-example:0.1.1"
$env:QT5CORE_IMAGE = $image
```

构建并前台测试：

```powershell
docker compose `
  up --build --force-recreate qt5core-example
```

确认日志和停止行为正常后，不要重新构建，直接推送同一个镜像：

```powershell
docker login registry.chengyistudio.com
docker push $image
docker buildx imagetools inspect $image
```

Harbor 清单必须包含：

```text
Platform: linux/arm64
```

## 8. ARM64 服务器从 Harbor 部署

```bash
IMAGE=registry.chengyistudio.com/cxx/qt5core-thread-example:0.1.1

docker login registry.chengyistudio.com
docker pull "$IMAGE"

docker run -d \
  --init \
  --name qt5core-thread-example \
  --restart unless-stopped \
  "$IMAGE"

docker logs -f qt5core-thread-example
```

停止：

```bash
docker stop qt5core-thread-example
```

确认日志包含：

```text
Stop signal received
QtCore worker thread stopped
```

## 9. 无互联网、局域网部署

Windows 导出已经测试的镜像：

```powershell
$image = "registry.chengyistudio.com/cxx/qt5core-thread-example:0.1.1"
$tar = ".\qt5core-thread-example-0.1.1-arm64.tar"

docker save --output $tar $image
Get-FileHash $tar -Algorithm SHA256
```

通过局域网传输：

```powershell
$remote = "root@192.168.100.2"

scp $tar "${remote}:/opt/uestcradar/images/"
```

ARM64 服务器导入并运行：

```bash
docker load \
  --input /opt/uestcradar/images/qt5core-thread-example-0.1.1-arm64.tar

docker run -d \
  --init \
  --name qt5core-thread-example \
  --restart unless-stopped \
  registry.chengyistudio.com/cxx/qt5core-thread-example:0.1.1
```

完整的 SHA256 校验、SCP 和回滚流程参见
[无互联网、局域网部署指南](../helloworld/OFFLINE.md)。

## 10. 替换为客户程序

拿到客户项目后按以下顺序迁移：

1. 将客户 `.pro`、`.pri`、源码和资源复制到新的工程目录。
2. 确认 Qt 版本：

   ```bash
   /usr/lib/qt5/bin/qmake -v
   ```

3. 为无界面模式添加：

   ```qmake
   QT += core
   QT -= gui widgets
   ```

4. 将 `QApplication` 或 `QGuiApplication` 入口替换为 `QCoreApplication`。
5. 从 headless 构建中排除窗口、`.ui` 和图形资源源码。
6. 修改 Dockerfile 的 `COPY`、`.pro` 文件名和最终 `TARGET`。
7. 使用 qmake 和 make 构建。
8. 对最终程序执行 `ldd`，不能存在 `not found`。
9. Windows 本地模拟运行通过后，再推送或通过 SCP 部署。

如果客户程序还使用其他 Qt 模块，例如 Network、SQL、SerialPort，应在 `.pro` 中保留对应模块，并在最终运行镜像加入相应 Qt 运行库和插件。

## 11. 基础镜像已经包含工具时

当前基础镜像经过实际检查没有 `g++`，因此示例 Dockerfile会安装编译工具和 Qt5 开发包。

如果后续换成客户提供的 Qt5 开发基础镜像，只有在下面命令全部成功后，才可以删除 builder 阶段的安装步骤：

```bash
g++ --version
make --version
/usr/lib/qt5/bin/qmake -v
test -f /usr/include/aarch64-linux-gnu/qt5/QtCore/QCoreApplication
```

运行阶段同理：只有基础镜像已经包含 `libQt5Core.so.5` 及其依赖时，才可以删除 `libqt5core5t64` 安装步骤。
