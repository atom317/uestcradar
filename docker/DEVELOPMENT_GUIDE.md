# ARM64 C++ 镜像本地调试、推送与部署指南

本文以 `workspace/hello.cpp` 为例，说明完整开发链路：

如果部署环境不能访问互联网和 Harbor，但 Windows 与 ARM64 服务器局域网互通，请改用
[无互联网、局域网版开发与部署指南](OFFLINE_DEVELOPMENT_GUIDE.md)。

```text
Windows AMD64 修改源码
        ↓
构建 linux/arm64 镜像
        ↓
在 Docker Desktop 中模拟运行并观察日志
        ↓
测试通过后推送同一个镜像到 Harbor
        ↓
ARM64 服务器拉取并持续运行
```

## 一、最短操作流程

### 1. Windows 本地调试

在项目根目录打开 PowerShell：

```powershell
Set-Location C:\home\cxx\uestcradar

$image = "registry.chengyistudio.com/cxx/helloworld:0.2.0"
$env:HELLOWORLD_IMAGE = $image

docker compose up --build --force-recreate helloworld
```

最后一条命令会依次完成：

1. 编译 ARM64 C++ 程序。
2. 构建并加载 ARM64 镜像。
3. 启动 ARM64 容器。
4. 在当前 PowerShell 窗口持续显示程序日志。

预期每秒看到一次：

```text
helloworld  | Hello, World!
helloworld  | Hello, World!
helloworld  | Hello, World!
```

按 `Ctrl+C` 停止运行。清理测试容器：

```powershell
docker compose down
```

### 2. 测试通过后推送 Harbor

不要重新构建，直接推送刚才本地测试过的同一个镜像：

```powershell
docker login registry.chengyistudio.com
docker push $image
docker buildx imagetools inspect $image
```

远端镜像清单必须包含：

```text
Platform: linux/arm64
```

### 3. ARM64 服务器部署

登录 ARM64 服务器后执行：

```bash
IMAGE=registry.chengyistudio.com/cxx/helloworld:0.2.0

docker login registry.chengyistudio.com
docker pull "$IMAGE"

docker run -d \
  --name helloworld \
  --restart unless-stopped \
  "$IMAGE"

docker logs -f helloworld
```

预期每秒看到一次：

```text
Hello, World!
```

以上就是最短的完整流程。下面解释每个阶段以及日常开发方式。

## 二、项目结构

```text
uestcradar/
├── .dockerignore
├── compose.yaml
├── docker/
│   ├── Dockerfile
│   ├── build-local.ps1
│   └── DEVELOPMENT_GUIDE.md
└── workspace/
    └── hello.cpp
```

- `workspace/hello.cpp`：C++ 源码。
- `docker/Dockerfile`：编译并封装 ARM64 程序。
- `compose.yaml`：Windows 本地构建和运行配置。
- `docker/build-local.ps1`：只构建镜像、不启动程序的一键脚本。
- `.dockerignore`：限制 Docker 构建上下文。

## 三、环境要求

Windows 开发机需要：

- AMD64 Windows。
- Docker Desktop 正在运行。
- Docker Desktop 使用 Linux containers。
- Buildx 支持 `linux/arm64`。
- 能拉取基础镜像 `registry.chengyistudio.com/cxx/ubuntu:24.04`。

检查 ARM64 支持：

```powershell
docker buildx inspect --bootstrap
```

输出的 `Platforms` 必须包含：

```text
linux/arm64
```

Docker Desktop 会通过 QEMU 在 AMD64 Windows 上模拟 ARM64。它适合功能测试，但性能不代表 ARM64 真机性能。

## 四、Windows 日常本地调试

### 1. 选择发布版本

每次准备发布时使用一个新版本号：

```powershell
$image = "registry.chengyistudio.com/cxx/helloworld:0.2.1"
$env:HELLOWORLD_IMAGE = $image
```

不要反复覆盖已经发布的版本。

### 2. 修改源码

编辑：

```text
workspace/hello.cpp
```

当前示例是持续运行程序，每秒打印一次日志。`std::endl` 会刷新标准输出，所以日志能立即显示。

### 3. 一条命令构建、启动并观察日志

```powershell
docker compose up --build --force-recreate helloworld
```

这是日常本地调试最推荐的命令。

- `--build`：源码变化后重新编译和构建镜像。
- `--force-recreate`：确保使用新镜像创建容器。
- 不加 `-d`：前台运行，直接显示日志。

修改源码后，按 `Ctrl+C`，再次执行同一条命令即可重新测试。

### 4. 后台运行方式

如果不希望当前 PowerShell 一直被日志占用：

```powershell
docker compose up -d --build --force-recreate helloworld
```

持续查看日志：

```powershell
docker compose logs -f helloworld
```

按 `Ctrl+C` 只退出日志查看，容器仍会在后台运行。

检查状态：

```powershell
docker compose ps
```

停止并删除本地测试容器：

```powershell
docker compose down
```

也可以在 Docker Desktop 的 `Containers` 页面打开 `helloworld`，查看状态和实时日志。

### 5. 只构建、不启动

如果只想将 ARM64 镜像构建并加载到 Docker Desktop：

```powershell
.\docker\build-local.ps1 -Image $image
```

注意：这条命令不会启动容器，因此不会出现持续的 `Hello, World!` 日志。

只构建完成后，可单独启动：

```powershell
docker compose up --force-recreate helloworld
```

### 6. 本地发布前检查

检查镜像架构和入口：

```powershell
docker image inspect $image `
  --format "architecture={{.Architecture}} os={{.Os}} entrypoint={{json .Config.Entrypoint}}"
```

预期结果：

```text
architecture=arm64 os=linux entrypoint=["/app/hello"]
```

检查容器是否仍在运行：

```powershell
docker inspect helloworld `
  --format "running={{.State.Running}} status={{.State.Status}}"
```

预期结果：

```text
running=true status=running
```

推送前至少确认：

- 镜像架构为 `arm64`。
- 容器能持续运行，没有立即退出。
- 日志按预期持续产生。
- 单元测试、配置测试和业务测试全部通过。

任何检查失败都禁止推送。

## 五、推送到 Harbor

### 1. 确认要推送的镜像

```powershell
$image
docker image inspect $image --format "{{.Id}} {{.Architecture}}"
```

必须确认这个变量对应刚才已经测试过的本地镜像。

### 2. 登录 Harbor

```powershell
docker login registry.chengyistudio.com
```

不要把 Harbor 密码写进 Dockerfile、Compose 或脚本。

### 3. 推送同一个镜像

```powershell
docker push $image
```

本地测试完成后不要再次执行构建。直接 `docker push` 才能保证推送的是已经测试过的同一份镜像。

### 4. 验证 Harbor 中的镜像

```powershell
docker buildx imagetools inspect $image
```

结果必须包含：

```text
Platform: linux/arm64
```

输出中的 `unknown/unknown` 通常是 Buildx 生成的构建证明清单，不是另一个可运行架构。

## 六、ARM64 服务器首次部署

### 1. 登录并拉取

```bash
IMAGE=registry.chengyistudio.com/cxx/helloworld:0.2.0

docker login registry.chengyistudio.com
docker pull "$IMAGE"
```

### 2. 检查镜像架构

```bash
docker image inspect "$IMAGE" \
  --format 'architecture={{.Architecture}} os={{.Os}}'
```

预期：

```text
architecture=arm64 os=linux
```

### 3. 后台持续运行

```bash
docker run -d \
  --name helloworld \
  --restart unless-stopped \
  "$IMAGE"
```

`--restart unless-stopped` 表示服务器或 Docker 重启后自动恢复运行，除非容器是被人工停止的。

### 4. 检查部署结果

查看状态：

```bash
docker ps --filter name=helloworld
```

持续查看日志：

```bash
docker logs -f helloworld
```

查看最近 20 行日志：

```bash
docker logs --tail 20 helloworld
```

退出持续日志查看使用 `Ctrl+C`，不会停止后台容器。

## 七、ARM64 服务器版本升级

假设从 `0.2.0` 升级到 `0.2.1`：

```bash
NEW_IMAGE=registry.chengyistudio.com/cxx/helloworld:0.2.1

docker pull "$NEW_IMAGE"
docker stop helloworld
docker rm helloworld

docker run -d \
  --name helloworld \
  --restart unless-stopped \
  "$NEW_IMAGE"

docker ps --filter name=helloworld
docker logs --tail 20 helloworld
```

先拉取新镜像，再停止旧容器，可以缩短停机时间。

如果新版本异常，可使用旧版本标签重新创建容器：

```bash
docker stop helloworld
docker rm helloworld

docker run -d \
  --name helloworld \
  --restart unless-stopped \
  registry.chengyistudio.com/cxx/helloworld:0.2.0
```

## 八、停止和卸载

临时停止：

```bash
docker stop helloworld
```

重新启动：

```bash
docker start helloworld
```

停止并删除容器：

```bash
docker stop helloworld
docker rm helloworld
```

删除容器不会自动删除镜像。

## 九、本地模拟测试的边界

Windows 上的 ARM64 模拟测试可以发现：

- 程序不能启动或异常退出。
- ARM64 ELF 架构错误。
- 镜像入口配置错误。
- 运行时动态库缺失。
- 基本业务逻辑、日志和端口异常。

它不能完全代替 ARM64 真机测试，尤其是：

- GPU、FPGA、雷达、串口等硬件设备。
- 宿主机驱动和内核模块。
- CPU 性能、实时性和 ARM 特定指令扩展。
- 生产服务器网络、存储卷和配置。

因此，Windows 测试通过后允许推送 Harbor，但 ARM64 服务器部署后仍必须进行冒烟测试。

## 十、常见问题

### 构建完成但看不到程序日志

原因：执行的是只构建脚本：

```powershell
.\docker\build-local.ps1
```

它不会启动程序。要构建、启动并观察日志，执行：

```powershell
docker compose up --build --force-recreate helloworld
```

### 容器立即退出

查看状态和日志：

```powershell
docker compose ps -a
docker compose logs helloworld
```

程序崩溃、入口错误或运行库缺失都会导致容器退出。

### 出现平台不匹配提示

Windows 是 AMD64，而镜像是 ARM64。Compose 已设置：

```yaml
platform: linux/arm64
```

Docker Desktop 会使用模拟运行。该提示本身不代表程序失败。

### 出现 `exec format error`

通常表示镜像或程序不是 ARM64：

```powershell
docker image inspect $image --format "{{.Architecture}}"
```

必须输出：

```text
arm64
```

### 修改源码后仍然看到旧结果

强制重新构建和创建容器：

```powershell
docker compose up --build --force-recreate helloworld
```

如果仍怀疑缓存：

```powershell
docker compose build --no-cache helloworld
docker compose up --force-recreate helloworld
```

### APT 无法连接 `host.docker.internal:7890`

Docker Desktop 配置了本机代理，但对应代理没有运行。当前 Dockerfile 在安装编译器时会清除这些代理环境变量。如果以后需要企业代理，应配置有效代理，不要把账号密码写入镜像。

## 十一、发布检查清单

推送 Harbor 前：

- [ ] 使用新的版本号。
- [ ] Windows 本地 ARM64 镜像构建成功。
- [ ] 镜像架构为 `linux/arm64`。
- [ ] 容器持续运行。
- [ ] 日志和功能符合预期。
- [ ] 单元测试及业务测试全部通过。
- [ ] 推送的是本地已经测试过的同一个镜像。
- [ ] Harbor 清单包含 `linux/arm64`。

ARM64 服务器部署后：

- [ ] 新镜像拉取成功。
- [ ] 容器状态为 `running`。
- [ ] 启动日志无异常。
- [ ] 冒烟测试通过。
- [ ] 已保留可回滚的上一版本标签。
