# UESTC Radar - 示例项目总览 (Examples Hub)

欢迎使用 **UESTC Radar SDK**！本目录提供了 5 个渐进式的示例项目。所有底层的零拷贝内存管理、通信与背压逻辑已被全盘封装，您可以根据实际业务场景选择最适合的开发模板。

---

## 🧭 选型决策树与示例速览

```text
               ┌──────────────────────────────────────────────┐
               │    您需要开发哪种类型的雷达数据处理应用？     │
               └──────────────────────┬───────────────────────┘
                                      │
           ┌──────────────────────────┴──────────────────────────┐
           ▼                                                     ▼
【单通道 / 标准算法解算】                               【多通道 / 高吞吐并行计算】
 推荐使用：algorithm                                     推荐使用：qt5-algorithm
 核心：单线程极简 C++20 管道                              核心：QThread 后台抽水 + 信号槽分发
```

| 示例项目 | 定位与核心特性 | 架构模式 | 详细说明文档 |
| :--- | :--- | :--- | :--- |
| 🌟 **[algorithm](./algorithm)** | **标准 C++20 核心算法模板**<br>极简单线程解算、IQ 帧转换、脉压帧生成与频谱导出 | 阻塞式管道 (`Input/Output`) | 📖 [查看详细教程](./algorithm/README.md) |
| 🌟 **[qt5-algorithm](./qt5-algorithm)** | **Qt5 多线程并行算法模板**<br>后台线程抽水 + 信号槽隐式共享分发，防算法卡顿丢帧 | 生产者-消费者 + 线程池 | 📖 [查看详细教程](./qt5-algorithm/README.md) |
| 🛠️ **[signalsource](./signalsource)** | **模拟雷达信号发生器**<br>持续产生 100Hz 测试波形推送到 Sidecar 数据井（基建） | 信号模拟发生器 | 📖 [查看详细教程](./signalsource/README.md) |
| 💡 **[helloworld](./helloworld)** | **极简 C++ 容器验证**<br>验证 Docker 运行与基础镜像编译环境 | 基础循环 | 源码文件 `src/main.cpp` |
| 💡 **[qt5core](./qt5core)** | **Qt5 事件循环集成**<br>验证 `QCoreApplication` 控制台事件循环与定时器 | 事件循环 | 源码文件 `src/main.cpp` |

---

## 💡 核心算法模板对比

### 1. [algorithm](./algorithm) —— 标准 C++20 核心算法模板
* **适用场景**：单通道雷达算法、轻量级解算、快速数学公式验证。
* **核心优势**：代码量极少，像写自然语言一样调用 `input.read()` 与 `output.write()`，底层细节完全透明。
* **架构流程**：`Input<IQFrame>` ➔ 时域解算 ➔ `Output<PulseCompressionFrame>`。
* **详细构建与运行指南** ➔ 见 [algorithm/README.md](./algorithm/README.md)。

---

### 2. [qt5-algorithm](./qt5-algorithm) —— Qt5 多线程并行算法模板
* **适用场景**：多通道阵列雷达、高吞吐大数据量解算、复杂算子（如 2D-FFT、CFAR 检波）。
* **核心优势**：
  1. **后台独立抽水**：`RadarReader` 专职从共享内存拉取数据，绝不阻塞。
  2. **跨线程零拷贝**：利用 Qt 信号槽隐式共享（Copy-on-write）机制，高效将通道数据投递给并行处理线程。
  3. **线程池解算**：各通道独立运行在专属 `QThread` 中，极限压榨多核性能。
* **详细构建与运行指南** ➔ 见 [qt5-algorithm/README.md](./qt5-algorithm/README.md)。

---

## 🎯 渐进式学习路线

1. **初次体验与环境校验** ➔ 运行 **[helloworld](./helloworld)** 验证环境。
2. **编写第一个雷达算法** ➔ 阅读 **[algorithm/README.md](./algorithm/README.md)** 并修改算法。
3. **构建高并发生产系统** ➔ 阅读 **[qt5-algorithm/README.md](./qt5-algorithm/README.md)** 引入多线程。
