# UESTC Radar - 模拟信号源开发模板 (Signal Source)

本目录是为**雷达基建开发工程师**或**需要自定义测试波形（如 FMCW 线性调频）的开发者**准备的模板。

如果您只是想开发雷达目标检测算法，请直接前往 `../algorithm/` 目录。
如果您的任务是模拟一台物理硬件雷达向后方输出信号，那么这就是您的开发脚手架。

## 第一步：一键启动“发端基座”

作为数据的生产者，您的数据需要被网络节点接管并发送出去。对您而言，网络底层细节被完全屏蔽，您只需要面对一个**“只发不收的数据管道入口”**。

请直接使用本目录下提供的 `docker-compose.infra.yaml` 建立起您的单节点网关基座：

```bash
docker compose -f docker-compose.infra.yaml up -d
```
启动成功后，网关基座已经开始监听，您的数据管道已敞开。

## 第二步：编写您的波形生成器

作为数据发送方，您唯一的目标就是源源不断地生成数据，并调用 SDK 将其泵入基础设施。
波形生成逻辑已经封装在 `my_waveform.hpp` 中。主程序（`main.cpp`）就像自然语言一样极简：

```cpp
#include "sdk.h"
#include "my_waveform.hpp"

int main() {
    // 1. 接入发送网关
    io_open();

    // 2. 生成模拟波形数据
    RadarSource::generate_sine_wave(buffer);

    // 3. 不断往外打数据，如果太快会自动阻塞 (背压机制)
    while (true) {
        io_write(buffer, size);
    }
    
    // 4. 关闭管道
    io_close();
}
```

请直接打开本目录 `src/` 下的 `main.cpp` 查看完整源码，它将作为您的开发模板。

## 第三步：打包并“热插拔”您的信号源

写好波形逻辑后，使用本目录下的 `Dockerfile` 将其打包为容器镜像。
注意：由于编译需要链接上层的 SDK，请务必将构建上下文指向项目根目录（即 `../../../`）：
```bash
docker build -t my-radar-signalsource:dev -f Dockerfile ../../../
```

最后，将您的发生器挂靠到基座节点 `sidecar-alpha` 身上启动：
```bash
docker run -d \
  --name radar-signalsource \
  --network host \
  --ipc container:sidecar-alpha \
  my-radar-signalsource:dev
```
此时，源源不断的测试雷达数据已经顺着网络流向接收端了！

## 第四步：发布镜像至私有源 (供算法团队使用)

当您在本地完成波形发生器的开发和验证后，**最关键的一步是将其推送到私有仓库**。
因为对算法开发者而言，他们运行的 `docker-compose.infra.yaml` 默认会从远端拉取名为 `ring-signalsource:latest` 的数据源镜像。只有您推送了，他们才能用到您新写的模拟波形。

```bash
# 1. 重新打上符合远端私有源规则的 Tag
docker tag my-radar-signalsource:dev registry.chengyistudio.com/cxx/ring-signalsource:latest

# 2. 登录私有仓库 (若未登录)
docker login registry.chengyistudio.com

# 3. 推送到远端
docker push registry.chengyistudio.com/cxx/ring-signalsource:latest
```

发布完成后，您就成功地为整个算法团队提供了一个全新的雷达测试基座！
