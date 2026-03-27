| Supported Target | ESP32-S3 |
| ---------------- | -------- |

# ota_example

`ota_example` 是一个基于 `ESP-IDF 6.0` 的 `ESP32-S3` 最小 USB Serial/JTAG IAP / OTA 工程。

## 功能

- `USB Serial/JTAG` 单口同时承担：
  - 日常命令通信
  - OTA 固件升级
- 使用 `otadata + ota_0 + ota_1` 的 A/B 分区切换
- 提供可复用的 `app` 生命周期框架：
- `app.c` 内部直接维护业务 `state struct`
- `app_task` 内部显式调用业务 `init/start/loop/on_received`
- OTA 控制台优先把收到的原始数据帧分发给 `app_on_received()`，用户处理后会跳过框架内建命令
- OTA 启动前会先检查 `app` 是否处于 `idle`，并在升级期间暂停 `app`
- 控制台命令：
  - `help`
  - `ping`
  - `info`
  - `echo <text>`
  - `reboot`
  - `ota <size> <sha256>`

## 工程结构

- `main/app_main.c`：只负责启动框架任务
- `main/app.c` / `main/app.h`：应用生命周期、全局状态、命令回调分发
- `main/ota_service.c`：USB 命令控制台、默认命令与 OTA 流程
- `scripts/cdc_ota.py`：PC 端升级脚本

## 构建与烧录

先确保已经加载 `ESP-IDF 6.0` 环境，然后执行：

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COM6 flash monitor
```

生成的应用镜像默认为：

```text
build/ota_example.bin
```

## OTA 验证

先确认命令链路正常：

```powershell
py scripts/cdc_ota.py --port COM6 info
py scripts/cdc_ota.py --port COM6 ping
```

再执行 OTA：

```powershell
py scripts/cdc_ota.py --port COM6 ota --image build/ota_example.bin --chunk-size 256 --chunk-delay-ms 10
```

升级完成后再次确认：

```powershell
py scripts/cdc_ota.py --port COM6 info
```

如果切换成功，通常会看到：

- `running=ota_1` / `boot=ota_1`
- 或下一次升级后回切到 `ota_0`

## 注意事项

- 分区表已改为 OTA 布局，首次请执行一次完整烧录。
- 当前验证稳定的主机参数是：
  - `--chunk-size 256`
  - `--chunk-delay-ms 10`
- 本工程当前使用的是 `USB Serial/JTAG`，不是 `USB CDC ACM`。
- 心跳默认每 `5s` 输出一次。
- 日志统一格式为：`[HH:MM:SS.mmm] [LEVEL] 内容`
