# 2026-03-27 ESP32-S3 USB Serial/JTAG OTA 调试记录

## 目标
- 使用 `ESP32-S3` 的同一个 `USB Serial/JTAG` 口完成：
  - 日常命令通信
  - 应用层 OTA 升级
- 采用 `ESP-IDF 6.0`
- 使用 A/B OTA 分区：`ota_0` / `ota_1`

## 最终可用结论
- `USB Serial/JTAG` 必须作为 **primary console**，不能只作为 secondary console。
- 命令读取和 OTA 二进制读取必须走 **同一套底层读取路径**。
- 在本项目中，稳定方案是统一使用 `usb_serial_jtag_read_bytes()`。
- OTA 成功后可通过 `info` 确认：
  - `running=ota_1`
  - `boot=ota_1`
- 当前验证通过的主机参数：
  - `--chunk-size 256`
  - `--chunk-delay-ms 10`

## 今天遇到的主要问题和经验

### 0. ESP32-S3-WROOM-1-N16R8 的 Flash / PSRAM 模式先配错了
**现象**
- 板子烧录后 bootloader 能正常起来。
- 日志能看到：
  - `SPI Flash Size : 16MB`
  - 但随后报 `quad_psram: PSRAM chip is not connected, or wrong PSRAM line mode`
  - 并在 `Failed to init external RAM!` 后 abort

**第一次误判**
- 一开始只把工程从 `2MB flash` 改成了 `16MB flash`，并打开了 `PSRAM`。
- 当时配置是：
  - Flash = `DIO`
  - PSRAM = `QUAD`
- 这会导致：
  - Flash 虽然能启动，但 `PSRAM` 初始化失败。

**逐步定位过程**
- 第一步先确认 `flash` 容量和分区表：
  - boot log 显示 `SPI Flash Size : 16MB`
  - OTA 分区表也正确加载
- 第二步把 Flash 从 `DIO` 改成 `QIO`
  - 改完后日志变成 `SPI Mode : QIO`
  - 说明 Flash 模式已经匹配
- 第三步继续看报错仍然是：
  - `quad_psram: ... wrong PSRAM line mode`
  - 这时可以确认问题只剩 `PSRAM` 线模式不匹配
- 最终根据模组型号 `ESP32-S3-WROOM-1-N16R8`，把 `PSRAM` 从 `QUAD` 改成 `OCTAL`

**最终正确配置**
- 模组：`ESP32-S3-WROOM-1-N16R8`
- Flash size：`16MB`
- Flash mode：`QIO`
- PSRAM：`8MB Octal`
- 关键结论：
  - `N16R8` 这块板在本项目里不能按 `quad psram` 配
  - 否则会在 app 启动前卡死在 external RAM 初始化阶段

**最终成功特征**
- 启动日志出现：
  - `octal_psram: vendor id ...`
  - `Found 8MB PSRAM device`
  - `SPI SRAM memory test OK`
  - `flash io: qio`
- 应用正常进入：
  - `OK cdc_iap_ready`

**经验**
- 对 `ESP32-S3-WROOM-1-N16R8`，板级配置要优先按：
  - `16MB Flash`
  - `QIO Flash`
  - `8MB Octal PSRAM`
- 如果看到：
  - `quad_psram: PSRAM chip is not connected, or wrong PSRAM line mode`
  - 不要先怀疑分区表或应用逻辑，先检查 `PSRAM` 模式是不是配成了 `QUAD`
- 排查顺序建议固定为：
  - 先看 `SPI Flash Size`
  - 再看 `SPI Mode`
  - 最后看 `quad_psram` / `octal_psram` 初始化日志

### 1. 把控制台从 USB Serial/JTAG 切到 USB CDC 后，COM 口消失
**现象**
- 烧录后原来的 `COM6` 消失。
- 重启后没有新的串口出来。

**原因**
- 板子原本稳定工作的口是 `USB Serial/JTAG`，不是项目里后来切换的 `USB CDC console`。
- 切到 `CONFIG_ESP_CONSOLE_USB_CDC` 后，枚举路径变了，且不适合当前板子/当前接法。

**经验**
- 对已经稳定工作的 ESP32-S3 板卡，不要轻易把 console 从 `USB Serial/JTAG` 改成 `USB CDC`。
- 先确认硬件上到底是哪一路 USB 在工作。

### 2. COM 口能看到心跳，但脚本 `ping` 写超时
**现象**
- 串口能看到心跳日志。
- `python cdc_ota.py --port COM6 ping` 报 `Write timeout`。

**原因**
- 当时配置成了：
  - primary console = `UART0`
  - secondary console = `USB Serial/JTAG`
- 导致 `COM6` 只适合输出，不一定能作为输入命令口。

**经验**
- 如果要单口做“日志 + 命令 + OTA”，必须把 `USB Serial/JTAG` 设为 **主控制台**。

### 3. OTA 期间出现心跳，导致怀疑逻辑没有切换
**现象**
- OTA 数据发到 100% 后，还看到 `HEARTBEAT ...`。

**原因**
- `s_ota_in_progress = true` 设置得太晚。
- 在打印 `READY` 前后存在小竞态窗口，心跳任务可能抢先输出一条日志。

**经验**
- 进入 OTA 前就先设置忙标志，再打印 `READY`。
- OTA 期间应暂停所有周期性日志输出，避免干扰同一条串口链路。

### 4. 主机发到 100%，但设备没有任何 OTA 完成日志
**现象**
- 主机侧显示 100%。
- 设备端没有 `OTA stage=write_done`。

**原因**
- 一开始命令读取用了 `fgets(stdin)`，OTA 二进制读取用了 `usb_serial_jtag_read_bytes()`。
- 文本读取和原始读取混用同一个 USB 通道，导致缓冲/抢读问题。
- 最终设备端没有真正收到完整镜像。

**经验**
- 同一个口不能混用：
  - 一套 VFS/stdin 读取
  - 一套底层 raw 读取
- 最稳的方案是：**命令头和二进制都统一走 raw 读取**。

### 5. 发送参数过快时 OTA 不稳定
**现象**
- 使用较大的 chunk 或过快发送时，设备端经常卡在接收尾部。

**原因**
- `USB Serial/JTAG` 在当前板卡/当前链路下，对大块连续发送不够稳定。

**经验**
- 先用保守参数跑通，再尝试提速。
- 已验证稳定参数：
  - `--chunk-size 256 --chunk-delay-ms 10`

### 6. `esp_partition_get_sha256()` 不能直接和主机文件 SHA 比较
**现象**
- OTA 数据发送完整，`esp_ota_end()` 成功。
- 但是出现 `sha256_mismatch`。

**原因**
- 使用了错误的校验思路：
  - 把分区 SHA 与主机原始文件 SHA 直接比较。
- 这个比较在当前流程下不成立。

**经验**
- 对 OTA 成功与否，优先依赖：
  - `esp_ota_end()` 的镜像验证
  - `esp_ota_set_boot_partition()` 成功
  - 重启后 `info` 确认 `running/boot` 切换
- 不要轻易把“分区 SHA”和“原始 bin 文件 SHA”直接等同。

### 7. `ESP-IDF 6.0` 里直接 `#include "mbedtls/sha256.h"` 编不过
**现象**
- 编译报错：`mbedtls/sha256.h: No such file or directory`

**原因**
- 当前 `ESP-IDF 6.0` 环境下，不适合直接按旧方式包含这个头。

**经验**
- 在本项目里没必要为了额外文件流 SHA 校验引入这层依赖。
- 先把 OTA 主流程跑通，依靠 `esp_ota_end()` 做镜像校验即可。

### 8. 主机脚本可能“看不到”最后的完成日志
**现象**
- 主机显示 100%，但看不到完成消息。

**原因**
- 如果设备重启前 flush 不够，或者串口在重启时瞬断，主机脚本可能抓不到最后一行。

**经验**
- 设备端在重启前必须：
  - `fflush(stdout)`
  - `usb_serial_jtag_wait_tx_done(...)`
  - 再 `esp_restart()`
- 主机脚本应允许“串口在重启时短暂断开”。

## 最终稳定实现要点
- `USB Serial/JTAG` 设为 primary console。
- `console_task` 使用 raw line reader：`usb_serial_jtag_read_bytes()`。
- OTA 二进制接收也使用 `usb_serial_jtag_read_bytes()`。
- OTA 期间暂停心跳输出。
- OTA 各阶段增加阶段化日志：
  - `write_done`
  - `ota_end_ok`
  - `image_validated`
  - `set_boot_ok`
- 重启前强制 flush。

## 已验证成功流程
1. 编译并烧录应用。
2. 使用脚本确认命令通信：
   - `python cdc_ota.py --port COM6 info`
3. 使用以下参数执行 OTA：
   - `python cdc_ota.py --port COM6 ota --image build\ota_example.bin --chunk-size 256 --chunk-delay-ms 10`
4. OTA 成功后重新查询：
   - `running=ota_1`
   - `boot=ota_1`

## 后续建议
- 把当前实现拆成独立模块：
  - `console`
  - `ota_service`
  - `heartbeat`
- 未来可以在确认稳定后逐步调大：
  - `chunk-size`
  - 减小 `chunk-delay-ms`
- 再做一次反向验证：
  - `ota_1 -> ota_0`


