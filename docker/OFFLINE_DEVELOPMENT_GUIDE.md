# ARM64 C++ 镜像无互联网、局域网部署指南

本文适用于以下网络环境：

- Windows AMD64 开发机不能访问互联网。
- Linux ARM64 服务器不能访问互联网和 Harbor。
- Windows 与 ARM64 服务器通过网线或交换机连接，局域网互通。
- Windows 可以通过 SSH/SCP 访问 ARM64 服务器。

这里的“无网络”准确含义是“无互联网”，不是两台电脑之间完全断网。

## 一、整体流程

Harbor 不可达时，用镜像文件和局域网传输替代 `docker push`、`docker pull`：

```text
有互联网流程                    无互联网、局域网流程
docker push Harbor        →     docker save 导出 TAR
Harbor 传输镜像           →     SCP 通过局域网传输 TAR
docker pull Harbor        →     docker load 导入 TAR
```

完整链路：

```text
Windows AMD64 修改 C++ 源码
        ↓
构建 linux/arm64 镜像
        ↓
Docker Desktop 本地模拟运行
        ↓
测试通过后 docker save
        ↓
生成 SHA256 校验文件
        ↓
SCP 通过局域网传到 ARM64 服务器
        ↓
服务器校验并 docker load
        ↓
启动容器并执行冒烟测试
```

## 二、局域网准备

### 1. IP 地址示例

两台设备必须处于同一网段。例如：

```text
Windows AMD64：192.168.100.1/24
Linux ARM64：  192.168.100.2/24
```

纯局域网直连不要求配置互联网网关。实际地址以现场网络规划为准。

### 2. ARM64 服务器启用 SSH

ARM64 服务器需要运行 SSH 服务，并允许 Windows 使用账号登录。

在服务器检查：

```bash
systemctl status sshd
```

部分发行版服务名为：

```bash
systemctl status ssh
```

### 3. Windows 检查 SSH/SCP

在 PowerShell 执行：

```powershell
Get-Command ssh
Get-Command scp
```

Windows 10/11 通常可以安装 OpenSSH Client 可选功能。

### 4. Windows 验证局域网

下面假设 ARM64 服务器地址是 `192.168.100.2`：

```powershell
$serverIp = "192.168.100.2"
$remote = "root@$serverIp"

Test-Connection $serverIp -Count 2
Test-NetConnection $serverIp -Port 22
ssh $remote "uname -m"
```

ARM64 服务器应返回：

```text
aarch64
```

如果 Ping 不通但 SSH 端口正常，可以继续使用；部分系统会禁用 ICMP。

## 三、完全离线构建的前置条件

局域网只能连接 Windows 和 ARM64 服务器，不能替代 Ubuntu 软件源或外部镜像仓库。

当前 `docker/Dockerfile` 首次构建需要：

- ARM64 基础镜像 `registry.chengyistudio.com/cxx/ubuntu:24.04`。
- Dockerfile 前端镜像。
- APT 下载并安装 `g++`。

因此，在 Windows 进入无互联网环境之前，至少需要成功完成一次 ARM64 构建，并保留：

- Docker Desktop。
- ARM64 基础镜像。
- BuildKit 构建缓存。
- 已安装 `g++` 的构建层缓存。
- 项目源码。

不要清理这些资源：

```text
docker builder prune
docker buildx prune
docker system prune
```

完全干净且没有缓存的离线 Windows 电脑，无法使用当前 Dockerfile 完成首次构建。

## 四、Windows 本地开发与调试

### 1. 进入项目

```powershell
Set-Location C:\home\cxx\uestcradar
```

### 2. 设置版本和镜像名

每次发布使用新的版本号：

```powershell
$version = "0.2.0"
$image = "registry.chengyistudio.com/cxx/helloworld:$version"
$env:HELLOWORLD_IMAGE = $image
```

即使无法访问 Harbor，也可以继续使用这个镜像名。它只是 Docker 本地标签，不会自动触发网络访问。

### 3. 修改源码

编辑：

```text
workspace/hello.cpp
```

### 4. 构建、启动并观察日志

如果本地镜像和 BuildKit 缓存已经准备完整：

```powershell
docker compose up --build --force-recreate helloworld
```

当前 PowerShell 应每秒显示：

```text
helloworld  | Hello, World!
helloworld  | Hello, World!
helloworld  | Hello, World!
```

按 `Ctrl+C` 停止，然后清理测试容器：

```powershell
docker compose down
```

### 5. 验证构建阶段不访问互联网

需要严格验证离线构建时，可显式禁用构建网络：

```powershell
docker buildx build `
  --platform linux/arm64 `
  --network none `
  --load `
  -f docker/Dockerfile `
  -t $image `
  .
```

如果该命令尝试下载基础镜像、Dockerfile 前端或 APT 包后失败，说明离线依赖没有准备完整。

### 6. 验证镜像架构

```powershell
docker image inspect $image `
  --format "id={{.Id}} architecture={{.Architecture}} os={{.Os}} entrypoint={{json .Config.Entrypoint}}"
```

预期包含：

```text
architecture=arm64 os=linux entrypoint=["/app/hello"]
```

### 7. 按实际网络模式测试

Hello World 不需要网络，可以完全禁用容器网络：

```powershell
docker run --rm -it --init `
  --network none `
  --platform linux/arm64 `
  $image
```

真实程序如果需要访问局域网设备，不要使用 `--network none`。可以使用默认 bridge 网络，并按需映射端口：

```powershell
docker run --rm -it --init `
  --platform linux/arm64 `
  -p 8080:8080 `
  $image
```

Windows Docker Desktop 的网络行为与 ARM64 Linux 真机不完全相同，真实局域网通信仍需在服务器上复测。

## 五、导出局域网交付包

只有本地测试全部通过后才允许导出。

### 1. 创建交付目录

```powershell
$packageName = "helloworld-$version-arm64"
$packageDirectory = Join-Path ".\dist" $packageName
$tarFile = Join-Path $packageDirectory "$packageName.tar"

New-Item `
  -ItemType Directory `
  -Force `
  $packageDirectory
```

### 2. 导出已测试镜像

```powershell
docker save `
  --output $tarFile `
  $image

if ($LASTEXITCODE -ne 0) {
    throw "镜像导出失败，禁止传输"
}
```

`docker save` 不会重新构建。它导出的是刚才在 Windows 上测试过的同一个镜像。

### 3. 生成 SHA256

```powershell
$hash = (Get-FileHash $tarFile -Algorithm SHA256).Hash.ToLower()
"$hash  $packageName.tar" |
  Set-Content `
    -Encoding ascii `
    (Join-Path $packageDirectory "SHA256SUMS")
```

检查：

```powershell
Get-ChildItem $packageDirectory
Get-Content (Join-Path $packageDirectory "SHA256SUMS")
```

交付目录应包含：

```text
helloworld-0.2.0-arm64.tar
SHA256SUMS
```

## 六、通过局域网传输到 ARM64 服务器

下面沿用：

```powershell
$serverIp = "192.168.100.2"
$remote = "root@$serverIp"
$remoteBase = "/opt/uestcradar/images"
```

### 1. 创建服务器目录

```powershell
ssh $remote "mkdir -p $remoteBase"
```

### 2. SCP 传输整个交付目录

```powershell
scp `
  -r `
  $packageDirectory `
  "${remote}:${remoteBase}/"
```

传输完成后检查服务器文件：

```powershell
ssh $remote "ls -lh $remoteBase/$packageName"
```

SCP 只使用 Windows 与 ARM64 服务器之间的局域网，不需要互联网或 Harbor。

如果 SSH 使用非默认端口，例如 `2222`：

```powershell
scp `
  -P 2222 `
  -r `
  $packageDirectory `
  "${remote}:${remoteBase}/"
```

对应 SSH 命令使用小写 `-p`：

```powershell
ssh -p 2222 $remote
```

## 七、ARM64 服务器校验并导入

登录服务器：

```powershell
ssh $remote
```

以下命令在 ARM64 Linux 服务器执行：

```bash
VERSION=0.2.0
PACKAGE_NAME=helloworld-${VERSION}-arm64
PACKAGE_DIR=/opt/uestcradar/images/${PACKAGE_NAME}
IMAGE=registry.chengyistudio.com/cxx/helloworld:${VERSION}

cd "$PACKAGE_DIR"
```

### 1. 校验传输完整性

```bash
sha256sum -c SHA256SUMS
```

必须输出：

```text
helloworld-0.2.0-arm64.tar: OK
```

校验失败时禁止导入和部署，应重新执行 SCP。

### 2. 导入镜像

```bash
docker load --input "${PACKAGE_NAME}.tar"
```

`docker load` 只读取本地文件，不访问 Harbor。

### 3. 检查架构

```bash
docker image inspect "$IMAGE" \
  --format 'id={{.Id}} architecture={{.Architecture}} os={{.Os}}'
```

预期包含：

```text
architecture=arm64 os=linux
```

## 八、ARM64 服务器首次部署

Hello World 不需要局域网服务，可以使用默认 Docker bridge 网络：

```bash
docker run -d \
  --init \
  --name helloworld \
  --restart unless-stopped \
  "$IMAGE"
```

检查状态：

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

## 九、真实程序的局域网模式

无互联网不代表容器必须使用 `--network none`。应根据真实程序选择网络模式。

### 1. 程序只主动访问局域网设备

使用默认 bridge 网络即可：

```bash
docker run -d \
  --init \
  --name my-app \
  --restart unless-stopped \
  "$IMAGE"
```

### 2. 局域网其他设备需要访问容器服务

映射服务端口，例如：

```bash
docker run -d \
  --init \
  --name my-app \
  --restart unless-stopped \
  -p 8080:8080 \
  "$IMAGE"
```

局域网设备通过 ARM64 服务器地址访问：

```text
http://192.168.100.2:8080
```

### 3. 程序依赖广播、组播或低层网络

Linux ARM64 服务器上可以考虑 host 网络：

```bash
docker run -d \
  --init \
  --network host \
  --name my-app \
  --restart unless-stopped \
  "$IMAGE"
```

`--network host` 会减少网络隔离，应确认端口冲突及安全边界后使用。

### 4. 程序完全不需要网络

才使用：

```text
--network none
```

## 十、局域网版本升级

Windows 使用新版本号重新执行：

1. 构建 ARM64 镜像。
2. 本地运行测试。
3. `docker save`。
4. 生成 SHA256。
5. SCP 到服务器。

服务器导入新版本：

```bash
VERSION=0.2.1
PACKAGE_NAME=helloworld-${VERSION}-arm64
PACKAGE_DIR=/opt/uestcradar/images/${PACKAGE_NAME}
NEW_IMAGE=registry.chengyistudio.com/cxx/helloworld:${VERSION}

cd "$PACKAGE_DIR"
sha256sum -c SHA256SUMS
docker load --input "${PACKAGE_NAME}.tar"
```

先导入成功，再替换容器：

```bash
docker stop helloworld
docker rm helloworld

docker run -d \
  --init \
  --name helloworld \
  --restart unless-stopped \
  "$NEW_IMAGE"

docker ps --filter name=helloworld
docker logs --tail 20 helloworld
```

## 十一、回滚

服务器应保留上一版本镜像和 TAR 包。

新版本异常时：

```bash
docker stop helloworld
docker rm helloworld

docker run -d \
  --init \
  --name helloworld \
  --restart unless-stopped \
  registry.chengyistudio.com/cxx/helloworld:0.2.0
```

如果旧镜像已被删除：

```bash
docker load \
  --input /opt/uestcradar/images/helloworld-0.2.0-arm64/helloworld-0.2.0-arm64.tar
```

## 十二、检查清单

Windows 传输前：

- [ ] 使用新的版本号。
- [ ] 本地 ARM64 镜像构建成功。
- [ ] 镜像架构为 `linux/arm64`。
- [ ] 容器持续运行且日志正常。
- [ ] 单元测试和业务测试全部通过。
- [ ] TAR 来自已经测试过的同一个镜像。
- [ ] 已生成 SHA256 校验文件。
- [ ] Windows 到 ARM64 服务器的 SSH/SCP 正常。

ARM64 服务器部署前：

- [ ] `sha256sum -c` 返回 `OK`。
- [ ] `docker load` 成功。
- [ ] 镜像架构为 `arm64`。
- [ ] 容器网络模式符合真实业务需求。
- [ ] 容器状态为 `running`。
- [ ] 启动日志无异常。
- [ ] 冒烟测试通过。
- [ ] 保留上一版本用于回滚。

## 十三、常见问题

### Windows 可以连接服务器，但 Docker 构建尝试访问互联网

局域网连通不等于能访问基础镜像仓库和 Ubuntu 软件源。说明本地基础镜像或 BuildKit 缓存不完整。

需要在进入无互联网环境前完成一次构建并保留缓存，或者后续建立局域网内部镜像仓库和软件源。

### SCP 提示连接超时

依次检查：

```powershell
Test-Connection $serverIp -Count 2
Test-NetConnection $serverIp -Port 22
```

然后检查服务器 SSH 服务、IP 地址、网线、交换机和防火墙。

### SCP 提示权限不足

先复制到用户有权限的目录，例如：

```powershell
scp -r $packageDirectory "${remote}:/tmp/"
```

再登录服务器移动：

```bash
mkdir -p /opt/uestcradar/images
mv /tmp/helloworld-0.2.0-arm64 /opt/uestcradar/images/
```

### SHA256 校验失败

禁止部署，重新执行 SCP，直到：

```bash
sha256sum -c SHA256SUMS
```

返回 `OK`。

### 是否可以直接把镜像推到 ARM64 Docker

Docker Engine 没有“把本地镜像直接推到另一台 Docker Engine”的通用命令。无 Harbor 时，推荐使用：

```text
docker save → SCP → docker load
```

这条链路可审计、可校验，也便于保留历史版本和回滚。
