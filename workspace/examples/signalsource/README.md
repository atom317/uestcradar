# UESTC Radar - 模拟信号源 (Signal Source)

> **⚠️ 注意：算法开发者无需阅读**
> 
> 算法开发工程师**没有必要**理解本目录下任何代码的业务逻辑。
> 本目录仅用于：**测试人员或框架底座开发者** 编写和发布模拟测试数据流（如测试用的正弦波、FMCW 等）。
> 
> 纯算法开发者请直接前往 `../algorithm/` 目录进行业务开发，基座会自动拉取本目录生成的镜像为您提供测试数据。

---

## 给框架开发者的备忘录

本目录维护了物理雷达的“模拟发生器”。当您修改了底层波形逻辑（`src/my_waveform.hpp`）后，请按照以下步骤更新测试基建镜像：

```bash
# 1. 退回根目录，利用项目上下文进行编译
cd ../../../
docker build -t my-radar-signalsource:dev -f workspace/examples/signalsource/Dockerfile .

# 2. 重新打标签并发布到私有源 (覆盖 latest，自动提供给全体算法开发成员)
docker tag my-radar-signalsource:dev registry.chengyistudio.com/cxx/ring-signalsource:latest
docker push registry.chengyistudio.com/cxx/ring-signalsource:latest
```

*开发调试可使用本目录下的 `docker-compose.infra.yaml` 启动单节点测试网关。*
