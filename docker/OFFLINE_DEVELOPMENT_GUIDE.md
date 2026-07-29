# ARM64 C++ 镜像本地调试、离线交付与部署指南

本文适用于 ARM64 服务器无法访问 Harbor，或者开发、部署环境完全隔离网络的情况。

无网络时不能执行 `docker push` 和 `docker pull`。对应替代关系是：

```text
联网流程                    无网络流程
docker push Harbor    →     docker save 导出 TAR
docker pull Harbor    →     docker load 导入 TAR
网络传输镜像          →     U 盘、移动硬盘等介质传输
```

完整链路：

```text
Windows AMD64 修改源码
        ↓
构建 linux/arm64 镜像
        ↓
本地模拟运行并观察日志
        ↓
docker save 导出 TAR + SHA256
        ↓
使用离线介质传到 ARM64 服务器
        ↓
校验 SHA256 + docker load
        ↓
启动容器并执行冒烟测试
```

## 一、先确认属于哪种离线场景

### 场景 A：只有 ARM64 服务器不能联网

Windows 开发机仍然可以访问基础镜像和 Ubuntu 软件源。这是最简单、最推荐的场景：

1. Windows 正常构建和本地测试。
2. 不推送 Harbor，改用 `docker save` 导出。
3. 通过 U 盘将 TAR 文件送到服务器。
4. 服务器执行 `docker load`。

### 场景 B：Windows 和 ARM64 服务器都不能联网

断网前必须在 Windows 上准备好：

- Docker Desktop。
- ARM64 基础镜像。
- Dockerfile 前端及 BuildKit 构建缓存。
- 已成功构建过至少一次的 ARM64 镜像。
- 项目源码。

当前 `docker/Dockerfile` 首次构建时需要通过 APT 安装 `g++`。完全离线且没有缓存的干净电脑无法完成首次构建。必须先在有网络的环境成功构建一次，并保留 Docker Desktop 的镜像与 BuildKit 缓存。

不要在进入离线环境前执行：

```text
docker builder prune
docker buildx prune
docker system prune
```

这些操作可能删除离线构建所需缓存。

## 二、Windows 本地调试

以下命令都在项目根目录执行：

```powershell
Set-Location C:\home\cxx\uestcradar
```

选择一个唯一版本：

```powershell
$image = "registry.chengyistudio.com/cxx/helloworld:0.2.0"
$env:HELLOWORLD_IMAGE = $image
```

镜像名称保留 Harbor 地址没有问题。离线部署时它只是本地镜像标签，不会自动访问 Harbor。

### 1. 有构建依赖时，一条命令调试

如果 Windows 仍可联网，或者完整构建缓存已经存在：

```powershell
docker compose up --build --force-recreate helloworld
```

该命令会构建 ARM64 镜像、启动容器并在当前窗口显示日志：

```text
helloworld  | Hello, World!
helloworld  | Hello, World!
helloworld  | Hello, World!
```

按 `Ctrl+C` 停止，再执行：

```powershell
docker compose down
```

### 2. 严格禁止构建阶段访问网络

完全离线开发时，可以用 `--network none` 验证构建只依赖本地镜像和缓存：

```powershell
docker buildx build `
  --platform linux/arm64 `
  --network none `
  --load `
  -f docker/Dockerfile `
  -t $image `
  .
```

如果该命令尝试下载 Dockerfile 前端、基础镜像或 APT 软件包后失败，说明离线资源没有准备完整。应回到有网络环境补齐依赖，而不是临时修改业务源码绕过。

### 3. 严格禁止运行阶段访问网络

前台运行并观察日志：

```powershell
docker run --rm -it --init `
  --network none `
  --platform linux/arm64 `
  $image
```

预期每秒输出：

```text
Hello, World!
```

按 `Ctrl+C` 停止。

使用 `--network none` 可以证明程序运行不依赖外部网络。

### 4. 后台测试

```powershell
docker run -d --init `
  --network none `
  --platform linux/arm64 `
  --name helloworld-offline-test `
  $image
```

查看日志：

```powershell
docker logs -f helloworld-offline-test
```

检查状态：

```powershell
docker inspect helloworld-offline-test `
  --format "running={{.State.Running}} status={{.State.Status}}"
```

停止并删除测试容器：

```powershell
docker stop helloworld-offline-test
docker rm helloworld-offline-test
```

## 三、导出离线交付包

只有本地测试全部通过后，才允许导出镜像。

### 1. 检查镜像

```powershell
docker image inspect $image `
  --format "id={{.Id}} architecture={{.Architecture}} os={{.Os}}"
```

必须包含：

```text
architecture=arm64 os=linux
```

### 2. 创建交付目录

```powershell
New-Item `
  -ItemType Directory `
  -Force `
  .\dist\helloworld-0.2.0-arm64
```

### 3. 导出镜像 TAR

```powershell
docker save `
  --output .\dist\helloworld-0.2.0-arm64\helloworld-0.2.0-arm64.tar `
  $image
```

`docker save` 保存的是已经在 Windows 本地测试过的同一个镜像，不会重新构建。

### 4. 生成 SHA256 校验文件

```powershell
$package = ".\dist\helloworld-0.2.0-arm64\helloworld-0.2.0-arm64.tar"
$hash = (Get-FileHash $package -Algorithm SHA256).Hash.ToLower()
"$hash  helloworld-0.2.0-arm64.tar" |
  Set-Content `
    -Encoding ascii `
    .\dist\helloworld-0.2.0-arm64\SHA256SUMS
```

检查交付目录：

```powershell
Get-ChildItem .\dist\helloworld-0.2.0-arm64
Get-Content .\dist\helloworld-0.2.0-arm64\SHA256SUMS
```

目录应包含：

```text
helloworld-0.2.0-arm64.tar
SHA256SUMS
```

### 5. 复制到离线介质

假设 U 盘盘符为 `E:`：

```powershell
Copy-Item `
  -Recurse `
  .\dist\helloworld-0.2.0-arm64 `
  E:\
```

安全弹出介质后，将其连接到 ARM64 服务器。

## 四、ARM64 服务器离线导入

假设离线包已经复制到服务器：

```text
/opt/offline/helloworld-0.2.0-arm64/
```

进入目录：

```bash
cd /opt/offline/helloworld-0.2.0-arm64
```

### 1. 校验文件完整性

```bash
sha256sum -c SHA256SUMS
```

必须输出：

```text
helloworld-0.2.0-arm64.tar: OK
```

如果校验失败，禁止执行 `docker load`。重新复制离线包并再次校验。

### 2. 导入镜像

```bash
docker load --input helloworld-0.2.0-arm64.tar
```

检查镜像：

```bash
IMAGE=registry.chengyistudio.com/cxx/helloworld:0.2.0

docker image inspect "$IMAGE" \
  --format 'id={{.Id}} architecture={{.Architecture}} os={{.Os}}'
```

预期包含：

```text
architecture=arm64 os=linux
```

`docker load` 只读取本地 TAR，不会访问 Harbor。

## 五、ARM64 服务器首次部署

后台运行并彻底禁用容器网络：

```bash
IMAGE=registry.chengyistudio.com/cxx/helloworld:0.2.0

docker run -d \
  --init \
  --network none \
  --name helloworld \
  --restart unless-stopped \
  "$IMAGE"
```

查看状态：

```bash
docker ps --filter name=helloworld
```

查看最近日志：

```bash
docker logs --tail 20 helloworld
```

持续查看日志：

```bash
docker logs -f helloworld
```

按 `Ctrl+C` 只退出日志查看，不会停止后台容器。

停止容器：

```bash
docker stop helloworld
```

重新启动：

```bash
docker start helloworld
```

## 六、离线版本升级

Windows 上使用新版本号重新完成：

1. 构建。
2. 本地测试。
3. `docker save`。
4. SHA256 校验文件生成。
5. 离线介质传输。

ARM64 服务器导入新版本：

```bash
cd /opt/offline/helloworld-0.2.1-arm64
sha256sum -c SHA256SUMS
docker load --input helloworld-0.2.1-arm64.tar
```

先导入新镜像，再停止旧容器：

```bash
NEW_IMAGE=registry.chengyistudio.com/cxx/helloworld:0.2.1

docker stop helloworld
docker rm helloworld

docker run -d \
  --init \
  --network none \
  --name helloworld \
  --restart unless-stopped \
  "$NEW_IMAGE"
```

检查状态和日志：

```bash
docker ps --filter name=helloworld
docker logs --tail 20 helloworld
```

## 七、离线回滚

不要在升级后立即删除旧版本镜像和旧离线包。

如果新版本异常：

```bash
docker stop helloworld
docker rm helloworld

docker run -d \
  --init \
  --network none \
  --name helloworld \
  --restart unless-stopped \
  registry.chengyistudio.com/cxx/helloworld:0.2.0
```

如果旧镜像已被删除，从旧 TAR 恢复：

```bash
docker load --input /opt/offline/helloworld-0.2.0-arm64/helloworld-0.2.0-arm64.tar
```

然后重新执行旧版本的 `docker run`。

## 八、离线交付检查清单

Windows 导出前：

- [ ] 使用新的版本号。
- [ ] ARM64 镜像构建成功。
- [ ] 镜像架构为 `linux/arm64`。
- [ ] 使用 `--network none` 完成运行测试。
- [ ] 程序持续运行且日志正常。
- [ ] 单元测试和业务测试全部通过。
- [ ] TAR 来自本地已经测试过的同一个镜像。
- [ ] 已生成 SHA256 校验文件。

ARM64 服务器导入后：

- [ ] `sha256sum -c` 返回 `OK`。
- [ ] `docker load` 成功。
- [ ] 镜像架构为 `arm64`。
- [ ] 容器状态为 `running`。
- [ ] 启动日志无异常。
- [ ] 冒烟测试通过。
- [ ] 保留上一版本镜像和离线包用于回滚。

## 九、常见问题

### 完全离线时构建失败并尝试下载软件包

原因是本地缺少基础镜像、Dockerfile 前端或 BuildKit 缓存。当前 Dockerfile 首次安装 `g++` 必须访问 Ubuntu 软件源。

处理方式：

1. 回到有网络的 Windows 环境。
2. 完整执行一次 ARM64 构建。
3. 确认镜像已经加载到 Docker Desktop。
4. 不要清理 Docker 镜像和 BuildKit 缓存。
5. 再进入离线环境。

### ARM64 服务器执行 `docker pull` 失败

无网络流程不执行 `docker pull`。使用：

```bash
docker load --input helloworld-0.2.0-arm64.tar
```

### TAR 校验失败

禁止继续部署。重新复制 TAR 和 `SHA256SUMS`，直到：

```bash
sha256sum -c SHA256SUMS
```

返回 `OK`。

### 容器无法联网

本指南部署命令主动使用了：

```text
--network none
```

这是预期行为。如果真实程序需要与服务器本机其他服务通信，需要根据实际拓扑配置本地 Docker 网络；但这不等同于允许访问互联网。
