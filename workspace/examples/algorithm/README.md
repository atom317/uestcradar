# UESTC Radar - 核心算法开发模板 (Algorithm)

欢迎！本目录是雷达**算法工程师**专用的开发模板。

通过底层的零拷贝技术和分布式网关，我们已经将复杂的网络通信、内存管理、背压阻塞全部封装成了“黑盒”。
对于算法开发者而言，世界上只有一个东西：**“一口源源不断涌出测试数据的雷达数据井”**。

## 第一步：一键启动“算法开发基座”

在开始写您的数学公式之前，您需要先用 `docker-compose` 把底层基础设施（包括模拟数据发生器、网络路由节点等）在后台启动。**这些细节对您完全屏蔽，您只需执行即可。**

请直接使用本目录为您准备好的 `docker-compose.infra.yaml` 启动基座：

```bash
docker compose -f docker-compose.infra.yaml up -d
```
启动成功后，水管已经接通，模拟数据已经堵在 `sidecar-beta` 的内存中，等待您的算法提取。

## 第二步：编写您的算法

在这个框架下，您完全不需要关心底层网络。所有的复杂度（包括通信、内存管理）都被封装在 `sdk.h` 中，而业务数学逻辑（如 FFT）则封装在 `my_algorithm.hpp` 中。

您的主程序（`main.cpp`）将变得像自然语言一样清晰易懂。以下是使用 SDK 的标准“四步曲”伪代码：

```cpp
#include "sdk.h"
#include "my_algorithm.hpp"

int main() {
    // 1. 接入基座的数据管道
    io_open();

    // 2. 无脑拉取测试数据
    io_read(buffer, size);
    
    // 3. 执行纯数学逻辑 (无需关心数据是怎么飞过来的)
    RadarAlgo::process_fft_and_save(buffer, output_path);
    
    // 4. 关闭管道
    io_close();
}
```

请直接打开本目录 `src/` 下的 `main.cpp` 查看完整源码，它将作为您的开发模板。

## 第三步：打包并“热插拔”您的算法

写好算法后，使用本目录极简的 `Dockerfile` 编译出您的算法容器。
注意：由于编译需要链接 SDK，请将构建上下文指向项目根目录（`../../../`）：
```bash
docker build -t my-radar-algorithm:dev -f Dockerfile ../../../
```

最后，带上 `--ipc container:sidecar-beta` 这把钥匙，将您的算法像插件一样挂载到基座上运行：
```bash
docker run -d \
  --name my-algorithm \
  --network host \
  --ipc container:sidecar-beta \
  -v $(pwd)/output:/output \
  my-radar-algorithm:dev
```

## 第四步：发布镜像至私有源 (用于生产部署)

当您的算法经过本地验证后，您可以将其推送到私有仓库，从而将算法部署到真实的物理雷达基站或云端集群中。

```bash
# 1. 打上符合私有源规则的 Tag
docker tag my-radar-algorithm:dev registry.chengyistudio.com/cxx/my-radar-algorithm:v1.0.0

# 2. 登录私有仓库 (若未登录)
docker login registry.chengyistudio.com

# 3. 推送到远端
docker push registry.chengyistudio.com/cxx/my-radar-algorithm:v1.0.0
```

至此，您的核心算法就已经成功发布，可以脱离开发环境独立运行了！
运行结束后，在本地的 `output` 文件夹查看 `fft_result.pgm` 的频谱结果，完成算法闭环验证！
