# UESTC Radar - 模拟信号源 (Signal Source)

> **⚠️ 注意：算法开发者无需阅读**
> 
> 本目录仅供框架底座开发者使用。算法开发者请前往 `../algorithm/`。

---

## 构建与发布

当您修改了波形逻辑后，请执行以下命令将最新的模拟源推送到私有仓库（这将自动更新所有算法开发者的测试基建）：

```bash
# 1. 在当前目录下直接执行构建（使用 --pull 确保拉取最新的 algo-base 镜像）
docker build --pull -t my-radar-signalsource:dev .

# 2. 重新打标签
docker tag my-radar-signalsource:dev registry.chengyistudio.com/cxx/ring-signalsource:latest

# 3. 发布到私有源 (覆盖 latest)
docker push registry.chengyistudio.com/cxx/ring-signalsource:latest
```
