| Supported Target | ESP32-S3 |
| ---------------- | -------- |

# ota_example

`ota_example` 是一个基于 `ESP-IDF 6.0` 的 `ESP32-S3` 最小 USB Serial/JTAG IAP / OTA 工程。

## 功能

- `USB Serial/JTAG` 单口同时承担：
  - 日常命令通信
  - OTA 固件升级
- 使用 `otadata + ota_0 + ota_1` 的 A/B 分区切换
- 保留 LED 闪烁任务与心跳任务，便于在线确认应用存活
- 控制台命令：
  - `help`
  - `ping`
  - `info`
  - `echo <text>`
  - `reboot`
  - `ota <size> <sha256>`

## 工程结构

- `main/app_main.c`：系统入口与任务创建
- `main/console_io.c`：USB Serial/JTAG 初始化、flush、行读取、信息输出
- `main/console_task.c`：命令解析与 OTA 命令调度
- `main/ota_service.c`：OTA 接收、写入、镜像校验、切换启动分区
- `main/heartbeat.c`：周期心跳
- `main/led.c`：LED 闪烁
- `main/app_state.h`：共享状态与公共常量
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
