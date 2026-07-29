# ARM64 C++ 镜像开发、测试与部署指南

本文以 `workspace/hello.cpp` 为例，说明如何在 AMD64 Windows 电脑上：

1. 使用 Docker Desktop 构建 `linux/arm64` C++ 镜像。
2. 在推送前通过 ARM64 模拟环境进行本地持续运行测试。
3. 只在测试通过后将同一个镜像推送到 Harbor。
4. 在 ARM64 服务器拉取并运行该镜像。

## 1. 项目文件

```text
uestcradar/
├── .dockerignore
├── compose.yaml
├── docker/
│   ├── Dockerfile
│   └── DEVELOPMENT_GUIDE.md
└── workspace/
    └── hello.cpp
```

- `workspace/hello.cpp`：每秒打印一次 `Hello, World!` 的常驻程序。
- `docker/Dockerfile`：ARM64 多阶段构建文件。
- `compose.yaml`：本地构建、运行和推送使用的 Compose 配置。
- `.dockerignore`：限制发送给 Docker 构建器的文件。

所有 Windows 命令都应在项目根目录执行：

```powershell
Set-Location C:\home\cxx\uestcradar
```

## 2. 环境要求

- Windows AMD64。
- Docker Desktop 正在运行，并使用 Linux containers。
- `docker buildx inspect --bootstrap` 的 `Platforms` 包含 `linux/arm64`。
- 已获得基础镜像的拉取权限。
- 推送前已通过 `docker login registry.chengyistudio.com` 登录 Harbor。

检查 ARM64 构建能力：

```powershell
docker buildx inspect --bootstrap
```

Docker Desktop 会使用 QEMU 模拟 ARM64。该方式适合功能测试和部署前冒烟测试，但速度通常低于 ARM64 真机。

## 3. 示例程序

`workspace/hello.cpp` 是一个持续运行程序：

```cpp
#include <chrono>
#include <iostream>
#include <thread>

int main() {
    while (true) {
        std::cout << "Hello, World!" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
```

`std::endl` 会刷新标准输出，因此可以通过 `docker logs` 实时看到每秒产生的日志。

## 4. 镜像构建原理

`docker/Dockerfile` 使用两个阶段：

1. `builder`：安装 `g++`、复制源码并编译 ARM64 程序。
2. `runtime`：只复制编译结果，并将 `/app/hello` 设置为容器入口。

编译发生在 `docker build` 过程中，产物会进入镜像层。不要依赖宿主机目录挂载后手动编译，因为挂载目录中的文件不属于镜像，无法随镜像推送到 Harbor。

## 5. 为每次发布选择唯一版本

不要反复覆盖已经发布的版本。下面以 `0.2.0` 为例：

```powershell
$image = "registry.chengyistudio.com/cxx/helloworld:0.2.0"
```

修改源码后，应增加版本号，例如 `0.2.1`、`0.3.0`。

## 6. 构建 ARM64 镜像并加载到 Windows

```powershell
docker buildx build `
  --platform linux/arm64 `
  --load `
  -f docker/Dockerfile `
  -t $image `
  .

if ($LASTEXITCODE -ne 0) {
    throw "ARM64 镜像构建失败，禁止测试和推送"
}
```

`--platform linux/arm64` 确保基础镜像、编译器和最终程序均为 ARM64。`--load` 将单平台构建结果加载到 Docker Desktop 本地镜像库，便于推送前测试。

## 7. 检查镜像架构和入口

```powershell
docker image inspect $image `
  --format "architecture={{.Architecture}} os={{.Os}} entrypoint={{json .Config.Entrypoint}}"
```

预期结果：

```text
architecture=arm64 os=linux entrypoint=["/app/hello"]
```

如果架构不是 `arm64`，立即停止后续流程。

## 8. 手动持续运行测试

启动本地 ARM64 测试容器：

```powershell
docker run -d `
  --rm `
  --platform linux/arm64 `
  --name helloworld-local-test `
  $image
```

确认容器持续运行：

```powershell
docker ps --filter "name=helloworld-local-test"
```

跟踪日志：

```powershell
docker logs -f helloworld-local-test
```

预期每秒出现一次：

```text
Hello, World!
Hello, World!
Hello, World!
```

按 `Ctrl+C` 只会退出日志跟踪，不会停止容器。测试完成后停止容器：

```powershell
docker stop helloworld-local-test
```

由于启动时使用了 `--rm`，容器停止后会自动删除。

## 9. 自动化测试熔断

下面的 PowerShell 流程检查：

- 容器在等待 4 秒后仍然运行。
- 日志中至少出现 3 次 `Hello, World!`。
- 无论测试成功还是失败，最终都会停止测试容器。

```powershell
$container = "helloworld-local-test"
$passed = $false

docker run -d `
  --rm `
  --platform linux/arm64 `
  --name $container `
  $image

if ($LASTEXITCODE -ne 0) {
    throw "测试容器启动失败，禁止推送"
}

try {
    Start-Sleep -Seconds 4

    $running = docker inspect `
      --format "{{.State.Running}}" `
      $container

    $logs = docker logs $container
    $lines = @($logs | Where-Object { $_ -eq "Hello, World!" })

    Write-Host "running=$running"
    Write-Host "heartbeat_count=$($lines.Count)"
    $logs

    $passed = ($running -eq "true" -and $lines.Count -ge 3)
}
finally {
    docker stop $container | Out-Null
}

if (-not $passed) {
    throw "ARM64 本地持续运行测试失败，禁止推送"
}

Write-Host "ARM64 本地测试通过，可以推送"
```

真实项目还应在推送前增加：

- CTest、GoogleTest 或项目自带单元测试。
- 配置文件加载测试。
- 服务端口或健康检查接口测试。
- 必要的输入、输出和异常场景测试。

任意测试返回非零退出码或断言失败，都必须终止推送。

## 10. 推送测试通过的同一个镜像

登录 Harbor：

```powershell
docker login registry.chengyistudio.com
```

推送本地已经测试过的镜像，不要重新构建：

```powershell
docker push $image

if ($LASTEXITCODE -ne 0) {
    throw "Harbor 推送失败"
}
```

这种方式确保本地测试和 Harbor 发布的是同一份镜像。

检查 Harbor 中的镜像清单：

```powershell
docker buildx imagetools inspect $image
```

结果中必须包含：

```text
Platform: linux/arm64
```

`unknown/unknown` 清单通常是 Buildx 生成的构建来源证明，不是另一个可运行的程序架构。

## 11. 使用 Docker Compose

当前 `compose.yaml` 已指定 `platform: linux/arm64`。

构建：

```powershell
docker compose build
```

后台启动：

```powershell
docker compose up -d
```

查看持续日志：

```powershell
docker compose logs -f helloworld
```

查看状态：

```powershell
docker compose ps
```

停止并删除容器：

```powershell
docker compose down
```

确认测试通过后推送 Compose 中声明的镜像：

```powershell
docker compose push
```

使用 Compose 时同样应先测试再推送。

## 12. ARM64 服务器部署

登录 Harbor：

```bash
docker login registry.chengyistudio.com
```

拉取镜像：

```bash
docker pull registry.chengyistudio.com/cxx/helloworld:0.2.0
```

检查架构：

```bash
docker image inspect \
  registry.chengyistudio.com/cxx/helloworld:0.2.0 \
  --format 'architecture={{.Architecture}} os={{.Os}}'
```

后台持续运行：

```bash
docker run -d \
  --name helloworld \
  --restart unless-stopped \
  registry.chengyistudio.com/cxx/helloworld:0.2.0
```

查看日志：

```bash
docker logs -f helloworld
```

检查容器状态：

```bash
docker inspect \
  --format '{{.State.Status}}' \
  helloworld
```

停止和删除：

```bash
docker stop helloworld
docker rm helloworld
```

## 13. 本地模拟测试的边界

Windows AMD64 上的 ARM64 模拟测试可以发现：

- 程序不能启动或异常退出。
- ELF 架构错误。
- 镜像入口配置错误。
- 运行时动态库缺失。
- 基本业务逻辑、日志和端口异常。

但它不能完全代替 ARM64 真机测试，尤其是：

- GPU、FPGA、雷达或串口等硬件设备。
- 宿主机驱动和内核模块。
- CPU 性能、实时性和特定 ARM 指令扩展。
- 服务器网络、存储卷和生产配置。

因此，Harbor 推送前执行 Windows ARM64 模拟测试，部署后仍需在 ARM64 服务器执行一次冒烟测试。

## 14. 常见问题

### 容器立即退出

查看退出码和日志：

```powershell
docker inspect helloworld-local-test `
  --format "{{.State.ExitCode}}"
docker logs helloworld-local-test
```

如果使用了 `--rm` 且容器已经退出，容器可能已被自动删除。调试时可以暂时去掉 `--rm`。

### 出现平台不匹配提示

Windows 是 AMD64，而镜像是 ARM64。显式添加下面的参数：

```text
--platform linux/arm64
```

Docker Desktop 将通过模拟运行。该提示本身不代表程序失败。

### 出现 `exec format error`

通常表示最终程序或镜像架构错误。重新检查：

```powershell
docker image inspect $image `
  --format "{{.Architecture}}"
```

并确认构建命令包含：

```text
--platform linux/arm64
```

### APT 无法连接 `host.docker.internal:7890`

Docker Desktop 配置了本机代理，但对应代理没有运行。当前 Dockerfile 在安装编译器的构建步骤中会清除这些代理环境变量。如果以后需要企业代理，应改成有效代理配置，而不是在源码或镜像里写入账号密码。

## 15. 发布检查清单

推送 Harbor 前必须全部满足：

- [ ] 使用了新的不可变版本号。
- [ ] Docker 构建成功。
- [ ] 镜像架构是 `linux/arm64`。
- [ ] 镜像入口正确。
- [ ] 容器持续运行，没有异常退出。
- [ ] 心跳日志数量符合预期。
- [ ] 单元测试和业务测试全部通过。
- [ ] 推送的是本地已测试的同一个镜像。
- [ ] Harbor 远端清单包含 `linux/arm64`。
- [ ] ARM64 服务器部署后完成冒烟测试。
