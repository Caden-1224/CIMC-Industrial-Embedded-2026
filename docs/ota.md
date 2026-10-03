# Flash 分区与 OTA 远程升级

本文描述 GD32F470 工业采集终端的 Flash 分区布局、App 与 BootLoader 之间的跨复位状态约定、两条升级路径的状态机、安装与回滚实现、跳转 App 的完整序列，以及配套的打包工具与静态验证脚本。工程由 `CIMC_App/` 与 `CIMC_BootLoader/` 两个 Keil 工程组成。

文档中的行号引用格式为 `相对路径:行号`，指向本次撰写时读取的源码版本。本次只做源码与静态脚本核对，**没有重新构建、烧录或进行掉电、硬件验收**；任何未做过实机验证的结论都在文中显式标注为「未验收」或「待核实」。RS485 帧格式与参数业务字段的完整语义不在本文范围内。

## 1. Flash 分区表

### 1.1 物理约束

| 项目 | 取值 | 依据 |
| --- | --- | --- |
| Flash 基址 | `0x08000000` | `CIMC_App/common/bl_partition.h:6` |
| Flash 总容量 | 512 KiB（`0x00080000`） | `CIMC_App/common/bl_partition.h:7` |
| 擦除粒度 | 4 KiB 页（`0x00001000`） | `CIMC_App/common/bl_partition.h:9` |
| 写入粒度 | 字编程（4 字节对齐）+ 字节编程尾部 | `CIMC_BootLoader/Function/bl_core.c:217`、`:229` |

分区常量在 App 与 BootLoader 两个工程中各有一份同名头文件（`CIMC_App/common/bl_partition.h` 与 `CIMC_BootLoader/common/bl_partition.h`），由编译期 `#error` 断言保持数值一致与不越界。

### 1.2 分区表

| 分区 | 起始地址 | 大小 | 别名 | 依据 |
| --- | --- | --- | --- | --- |
| BootLoader | `0x08000000` | 64 KiB（`0x00010000`） | — | `CIMC_App/common/bl_partition.h:11-L13` |
| 参数区（整页） | `0x08010000` | 4 KiB（`0x00001000`） | — | `CIMC_App/common/bl_partition.h:15-L16` |
| APP1（运行槽） | `0x08011000` | 128 KiB（`0x00020000`） | — | `CIMC_App/common/bl_partition.h:18-L20` |
| APP 备份区 | `0x08031000` | 128 KiB（`0x00020000`） | — | `CIMC_App/common/bl_partition.h:22-L24` |
| 下载暂存区 | `0x08051000` | 128 KiB（`0x00020000`） | APP2 | `CIMC_App/common/bl_partition.h:26-L28`、`:30-L32` |

`BL_APP2_START_ADDR` / `BL_APP2_SIZE` / `BL_APP2_END_ADDR` 是下载暂存区的别名，不占独立空间（`CIMC_App/common/bl_partition.h:30`）。`bl_param_t.app2_addr` 持久化的就是该别名地址（`CIMC_App/common/bl_param.h:47`）。因此「APP2」与「下载暂存区」是同一块物理区域。

### 1.3 参数区页内布局

参数区只占一个 4 KiB 页：

| 偏移 | 内容 | 依据 |
| --- | --- | --- |
| `+0x000` | 主 `bl_param_t`（280 字节） | `CIMC_App/common/bl_param.h:11` |
| `+0x100` | 备 `bl_param_t`（280 字节） | `CIMC_App/common/bl_param.h:12` |
| `+0x200` | 32 条 `bl_log_entry_t` 运行日志（每条 32 字节，共 1 KiB） | `CIMC_App/common/bl_param.h:13-L15` |
| `+0x600` | App 侧告警记录区（不在本文范围） | `docs/dev-notes/PROJECT_MEMORY.md:155` |

主、备副本相距 256 字节，但**位于同一个 4 KiB 擦除页内**。这是后文「已知边界」中参数掉电风险的根本原因。

### 1.4 编译期断言

| 断言 | 目的 | 依据 |
| --- | --- | --- |
| `BL_BOOT_END_ADDR + 1 == BL_PARAM_START_ADDR` | BootLoader 与参数页连续，不留空洞 | `CIMC_App/common/bl_partition.h:34-L36` |
| `BL_PARAM_START_ADDR + BL_PARAM_SIZE == BL_APP1_START_ADDR` | 参数页与 APP1 连续 | `CIMC_App/common/bl_partition.h:38-L40` |
| `BL_APP1_SIZE <= BL_APP_BACKUP_SIZE` | 备份区容得下 APP1 | `CIMC_App/common/bl_partition.h:42-L44` |
| `BL_DOWNLOAD_SIZE <= BL_APP1_SIZE` | 下载区不接受大于 APP1 的镜像 | `CIMC_App/common/bl_partition.h:46-L48` |
| `BL_DOWNLOAD_END_ADDR <= BL_FLASH_END_ADDR` | 下载区不越出内部 Flash | `CIMC_App/common/bl_partition.h:50-L52` |

这些断言只保证软件常量自洽，不等于擦写边界已经在硬件上实测安全。

### 1.5 擦除与写入的真实粒度

「按任意字节擦除」在这套硬件上不成立：

- 擦除只能整页。`bl_flash_erase_pages()` 先把 `start_addr` 和 `end_addr` 向下对齐到 4 KiB，再逐页调用 `fmc_page_erase()`（`CIMC_BootLoader/Function/bl_core.c:181-L195`，App 侧等价实现见 `CIMC_App/Function/BootUpdate_service.c:79-L95`）。因此任何一次「写一小段数据」都可能实际擦掉 1 页甚至跨页的 4 KiB 倍数。
- 写入按字节/字编程，且**不能把 1 改回 0**。`bl_flash_program_bytes()` 对 4 字节对齐的部分用 `fmc_word_program()`，剩余尾部用 `fmc_byte_program()`（`CIMC_BootLoader/Function/bl_core.c:217-L235`）。要复写同一地址必须先擦除。
- 参数区、告警区、BootLoader 日志共用同一个 4 KiB 页，所以任何一次参数提交都是「整页读出到 RAM → 补齐 → 整页擦除 → 整页回写」。BootLoader 侧见 `bl_commit_param_page()`（`CIMC_BootLoader/Function/bl_core.c:310-L343`），App 侧见 `AppParam_Service_Commit()`（`CIMC_App/Function/App_param_service.c:196-L226`）与 `param_commit_update()`（`CIMC_App/Function/BootUpdate_service.c:167-L206`）。

App 侧驱动 `CIMC_App/Driver/InternalFlash/InternalFlash.c` 提供的接口没有返回值，也不带写后回读：`InternalFlash_ErasePage()`（`:11-L18`）擦除后直接返回，`InternalFlash_WriteBytes()`（`:29-L51`）逐字节 `fmc_byte_program()` 并在 `while` 中空等忙标志、无超时。该驱动只用于参数与告警提交路径；升级镜像搬运走的是 `bl_core.c` 内带超时等待和返回值的实现。

## 2. 跨复位参数块

### 2.1 `bl_param_t` 结构

App 与 BootLoader 共享同一结构定义（`CIMC_App/common/bl_param.h:41-L56`、`CIMC_BootLoader/common/bl_param.h:60-L75`）：

| 字段 | 含义 | 备注 |
| --- | --- | --- |
| `magic` | `0x5AA5C33C` | 固定标识 |
| `version` | `0x00010001` | 结构版本 |
| `update_flag` | 升级状态字 | 见 2.2 |
| `app_size` | APP1 镜像有效长度 | 不含官方 bin 的 4 字节魔术字 |
| `app_crc32` | APP1 镜像 CRC32 | 只覆盖 `app_size` 字节 |
| `app1_addr` | APP1 地址 | 必须等于 `BL_APP1_START_ADDR` |
| `app2_addr` | 下载暂存区地址 | 必须等于 `BL_APP2_START_ADDR` |
| `update_counter` | 安装成功次数 | 安装成功时 +1 |
| `fail_counter` | 失败次数 | 各类失败路径 +1 |
| `last_error` | 最近错误码 | 见 2.3 |
| `log_write_index` | BootLoader 日志环形写指针 | 取模 32 |
| `reserved[51]` | 预留区，含 App 业务参数块 | 见 2.4 |
| `param_crc32` | 本块 CRC32 | 覆盖 `param_crc32` 之前的全部字节 |
| `tail_magic` | `0xA5A5C3C3` | 结构尾部标识 |

CRC 覆盖长度由 `offsetof(bl_param_t, param_crc32)` 决定，即 272 字节（`CIMC_BootLoader/Function/bl_core.c:87-L90`、`CIMC_App/Function/App_param_service.c:159`）。`reserved[]` 为 `uint32_t[51]`，共 204 字节，`bl_param_t` 总长 280 字节，可容纳在一个 256 字节槽位中的头部 0x00–0xFF 区域内（主副本 `+0x000`、备副本 `+0x100`）。

### 2.2 升级状态字取值

| 宏 | 取值 | 语义 | 依据 |
| --- | --- | --- | --- |
| `BL_UPDATE_FLAG_IDLE` | `0x00000000` | 无待处理升级 | `CIMC_App/common/bl_param.h:21` |
| `BL_UPDATE_FLAG_PENDING` | `0xAA55AA55` | App 流式路径已把完整镜像写入下载区，等待安装 | `CIMC_App/common/bl_param.h:22` |
| `BL_UPDATE_FLAG_FAILED` | `0xDEAD0001` | 安装失败 | `CIMC_App/common/bl_param.h:23` |
| `BL_UPDATE_FLAG_FAST_UPGRADE` | `0x55AA55AA` | 请求进入 BootLoader 等待固件 | `CIMC_App/common/bl_param.h:24` |
| `BL_UPDATE_FLAG_DOWNLOADED` | `0xA5A55A5A` | 下载区已有校验通过的镜像，等待 `0x0503` 安装 | `CIMC_App/common/bl_param.h:25` |

`FAST_UPGRADE` 与 `PENDING` 语义刻意区分：前者表示「重启进 BootLoader 等包」，后者表示「下载区已有完整镜像，应立即安装」。这一区分写在 App 侧注释中（`CIMC_App/Function/BootUpdate_service.c:213-L218`）。BootLoader 头文件声明了 `PENDING`，但当前没有任何分支消费它（见第 6 节与第 11 节）。

### 2.3 错误码

| 宏 | 取值 | 典型触发点 | 依据 |
| --- | --- | --- | --- |
| `BL_ERR_NONE` | `0` | — | `CIMC_App/common/bl_param.h:27` |
| `BL_ERR_PARAM_INVALID` | `1` | 主备参数均失效，恢复默认 | `CIMC_BootLoader/Function/bl_core.c:1266` |
| `BL_ERR_APP2_INVALID` | `2` | 下载区长度/向量/CRC 校验失败 | `CIMC_BootLoader/Function/bl_core.c:1068`、`:1084`、`:1095` |
| `BL_ERR_COPY_FAILED` | `3` | 搬运到 APP1 后 CRC 复核不一致或搬运本身失败 | `CIMC_BootLoader/Function/bl_core.c:1138`、`:1151` |
| `BL_ERR_APP1_INVALID` | `4` | APP1 向量表无效，BootLoader 无处可跳 | `CIMC_BootLoader/Function/bl_core.c:1362` |

### 2.4 与 App 业务参数共页

App 把业务参数块（设备 ID、波特率编码、上报间隔、告警模式、两路变比、三路阈值、DAC 原始值）写进 `bl_param_t.reserved[32]` 起的 16 个字（`CIMC_App/Function/App_param_service.c:8-L10`、`:214`）。BootLoader 侧有一份**逐字段镜像**的结构定义用于校验同一段数据（`CIMC_BootLoader/Function/bl_core.c:42-L61`、`:142`）。两侧字段布局必须保持一致，否则 CRC 校验会失败。

由此产生一个重要性质：**App 改一次设备 ID，就会连带重写整个参数页，包括升级状态字与 BootLoader 日志区**。参数页是全页保护性回写的（`CIMC_App/Function/App_param_service.c:218-L223`），所以升级元数据不会被覆盖成垃圾，但会经历一次擦除窗口。

### 2.5 BootLoader 的选择规则

`bootloader_run()` 启动时的决策（`CIMC_BootLoader/Function/bl_core.c:1244-L1276`）：

| 主副本有效 | 备副本有效 | 结果 | 是否补写修复 |
| --- | --- | --- | --- |
| 是 | 是 | 取 `update_counter` 较大者；相等时取主 | 仅当取备份时标记 `need_repair` |
| 是 | 否 | 取主 | 是 |
| 否 | 是 | 取备份 | 是 |
| 否 | 否 | 恢复默认值，并置 `last_error = BL_ERR_PARAM_INVALID` | 是 |

「有效」的定义比只查魔数更严：除 `magic`/`tail_magic`/`version` 外，还要求 `app1_addr`、`app2_addr` 等于分区常量，`app_size` 不超过 `BL_APP1_SIZE` 与 `BL_APP2_SIZE`，且 `param_crc32` 匹配（`CIMC_BootLoader/Function/bl_core.c:265-L295`）。需要修复时，会先准备一条 `BL_LOG_EVENT_PARAM_RECOVER` 日志，递增 `log_write_index` 再整页提交（`CIMC_BootLoader/Function/bl_core.c:1271-L1275`）。

### 2.6 与 App 侧选择规则的不一致

App 侧 `AppParam_Service_LoadBootParam()` 的规则不同（`CIMC_App/Function/App_param_service.c:163-L194`）：

| 对比项 | BootLoader | App |
| --- | --- | --- |
| 两份都有效时 | 比较 `update_counter` 取大 | **直接取主副本，不比较计数器** |
| 有效性判据 | 魔数 + 版本 + 尾魔数 + 两个地址 + `app_size` 上下界 + CRC | 魔数 + 版本 + 尾魔数 + 两个地址 + CRC（不查 `app_size`） |
| 两份都失效时 | 恢复默认并**回写**参数页 | 只在 RAM 里拼一个默认块，不落盘，且该块 `param_crc32` 保持为 0 |
| 修复动作 | 会写日志并提交 | 无 |

差异的实际后果是：如果主副本是旧值而备副本是安装成功后的新值（例如某次提交在写主副本之后、写备副本之前掉电），BootLoader 会选备份，而 App 在下一次 `LoadBootParam` 时会选主副本。此时 App 看到的 `update_flag`/`app_size` 可能与 BootLoader 实际使用的不一致。当前代码没有让两侧共享同一套选择逻辑，这一点属于待收敛项。

App 侧还有一个需要注意的细节：两份都失效时构造的默认块（`CIMC_App/Function/App_param_service.c:187-L193`）没有计算 `param_crc32`。该块只在 RAM 中作为 `AppParam_Service_Commit()` 的输入基底使用，Commit 会在写入前重算 CRC（`:215`），所以不会直接把 0 写进 Flash；但如果后续新增了直接持久化该默认块的路径，需要同步补上 CRC。

### 2.7 参数页完整性与 App 侧写入失败处理

App 侧参数提交分两条实现：

- `AppParam_Service_Commit()` 调 `InternalFlash_ErasePage()` / `InternalFlash_WriteBytes()`，两个函数都不返回结果，函数本身直接 `return 1U`（`CIMC_App/Function/App_param_service.c:222-L225`）。因此 App 返回给 RS485 的 OK 应答**不代表持久写入已经成功**（未验收）。
- `BootUpdate_Service_RequestEnterBootloader()` 与 `param_commit_update()` 使用带返回值的本地实现（`CIMC_App/Function/BootUpdate_service.c:246-L251`、`:200-L205`），失败时返回 0，`0x0501` 会因此回错误帧（`CIMC_App/Function/Rs485_service.c:1024-L1032`）。升级相关路径的写入结果是检查过的。

## 3. 两条升级路径总览

工程里存在两条互相独立的升级链路，硬件通路、包格式、状态字和安装责任方都不同，不能混答：

| 维度 | 官方 RS485 路径 | USART0 App 流式路径 |
| --- | --- | --- |
| 物理口 | USART1 / RS485（PD5/PD6，DE=PE8） | USART0（PA9/PA10），115200 8N1 DMA |
| 包格式 | RS485 ASCII-Hex 协议帧 + 裸 bin（前 4 字节魔术字 `5A A5 C3 3C`） | 4 字节小端 `ota_stream_header_t` 包头 + 裸 bin |
| 接收方 | BootLoader | App（`BootUpdate_service`） |
| 进入方式 | App 收 `0x0501` → 存 `FAST_UPGRADE` → 复位 | 直接向 App 灌流，无需先请求 |
| 落地状态 | `DOWNLOADED`（等 `0x0503`） | `PENDING` |
| 安装方 | BootLoader | 无（当前缺失） |
| 当前是否闭环 | 是 | **否**，见 6.3 节 |

两条路径的 CRC32 算法完全相同（同一份 `CimcProtocol_Crc32*` 实现），所以元数据可以互相对照，但包头格式与校验字段不可互换。

## 4. 路径 A：官方 RS485 升级

### 4.1 状态机（阶段名称为本文分析命名，非源码枚举）

| # | 状态 | 前置条件 | 动作 | 持久化内容 | 可能掉电的位置 |
| --- | --- | --- | --- | --- | --- |
| A1 | App 运行中 | 参数页可读 | 等待 `0x0501` 命令帧 | 无 | — |
| A2 | 进入请求已受理 | 帧 `frame_type == COMMAND`、`command == 0x0501`、`content_len == 0` | 读主备参数、选有效副本、置 `update_flag = FAST_UPGRADE`、重算 CRC、整页提交 | 参数页整页重写：`FAST_UPGRADE` + 清零 `last_error` | **整页擦除窗口**：擦除中掉电，主备两份可能同时失效（见 11 节第 3 项） |
| A3 | 已应答待复位 | 页提交成功 | 先发 `0x0501` OK，再延时 1500 ms 软复位 | 同 A2 | 复位前掉电：下次上电仍按 `FAST_UPGRADE` 进 BootLoader，行为与正常路径一致 |
| A4 | BootLoader 启动判定 | 上电/复位 | 读主备参数并按 2.5 节规则选择 | 若需修复则补写参数页 | 修复性补写同样经过整页擦除 |
| A5 | 等待 `0x0502` | `update_flag == FAST_UPGRADE` | 清 RX 流与命令扫描器、把 `update_flag` 提交为 `IDLE`、打印初始化提示与 10 s 倒计时、逐字节扫描 `A5B6…B6A5` 命令行 | 参数页写回 `IDLE` | 此处已清旗：若在收到 `0x0502` 之前掉电，下次上电按正常启动跳 App，升级会话丢失，需上位机重发 `0x0501` |
| A6 | 接收固件 | 收到合法 `0x0502`（设备 ID 匹配当前持久化 ID 或广播） | 逐字节从 RX 环取数据；校验 4 字节魔术字 `5A A5 C3 3C`；剥离魔术字后暂存 RAM（上限 16 KiB）；增量累计 payload CRC32；1.5 s 空闲或 30 s 总超时结束 | 无（仅 RAM） | 掉电：RAM 全丢，参数页仍是 `IDLE`，下次上电正常跳 App，无残留状态 |
| A7 | 写入下载区 | 收到过数据 + 魔术字完整 + payload 非空 + RAM 向量表合法 | 按 payload 长度向上对齐 4 KiB 计算擦除窗口、擦下载区、尾部补 `0xFF` 到页边界、整窗编程、再次校验下载区向量表 | 无 | **擦除中掉电**：下载区内容不确定，但无持久标记，下次上电仍跳旧 App；下载区被破坏不影响运行槽 |
| A8 | 已下载待安装 | 写入成功 | 置 `update_flag = DOWNLOADED`、`app_size = payload 长度`、`app_crc32 = payload CRC`、提交参数页，然后发 `0x0502` OK | `DOWNLOADED` + `app_size` + `app_crc32` | 提交过程中掉电：擦除窗口内主备可能同时失效，回落到默认参数（`FAILED`/`IDLE`），升级需重来 |
| A9 | 校验失败重试窗口 | A6/A7 任一步失败 | 把 `update_flag` 重新置回 `FAST_UPGRADE`、清空 `app_size`/`app_crc32`、`last_error = BL_ERR_APP2_INVALID`、`fail_counter++`、提交，静默等待下一个 `0x0502` | `FAST_UPGRADE` + 递增的 `fail_counter` | 提交过程中掉电同 A2 |
| A10 | 等待 `0x0503` | `update_flag == DOWNLOADED` | 60 s 内等待 `0x0503`；超时则置 `FAILED`、`fail_counter++`、写失败日志、回 `0x0503` ERROR、复位 | 超时路径写 `FAILED` | 超时提交掉电：可能停在 `DOWNLOADED`，下次上电仍在 A10 重新等 `0x0503` |
| A11 | 安装中 | 收到 `0x0503` | 先发 `0x0503` OK；随后 `__disable_irq()`，执行安装（见第 5 节），在临界区里提交参数与日志，再开中断 | 成功：`IDLE` + `update_counter++`；失败：`FAILED` + `fail_counter++` | **破坏性窗口**：从发 OK 到提交完成之间掉电，运行槽可能处于半写状态；参数未落盘时为 `DOWNLOADED`，下次上电会重新等 `0x0503` |
| A12 | 复位重启 | 提交完成 | 若安装或提交失败则补发 `0x0503` ERROR；随后 `NVIC_SystemReset()` | 已在 A11 落盘 | 复位后按 A4 重新判定；`IDLE` 且 APP1 向量有效则跳 App |

### 4.2 关键交互时序上的两点设计

- **先 ACK 再破坏**：`0x0503` 的 OK 帧在 `bl_install_downloaded_image()` 之前发出（`CIMC_BootLoader/Function/bl_core.c:1338-L1340`），这是上位机状态机的要求。代价是 OK 之后有一段无输出的 Flash 操作窗口。
- **ACK 后关中断**：`bl_send_ok_frame(0x0503U)` 之后立即 `__disable_irq()`，安装与参数提交都在临界区内完成，`__enable_irq()` 之后才判失败并决定是否补发错误帧（`CIMC_BootLoader/Function/bl_core.c:1338-L1350`）。原因是内部 Flash 忙期间若 USART1 中断进入，ISR 与被中断的代码都在同一片内部 Flash 上取指。这是本轮改动记录中定位到的现象（`docs/dev-notes/PROJECT_MEMORY.md:239`），属于设计权衡而非通用结论（同一文件也记录了此前两次不同的根因假设）。

### 4.3 固件流接收的工程细节

- **魔数剥离**：官方 bin 以 `5A A5 C3 3C` 开头，这 4 字节不写入下载区，否则下载区首字会变成魔术字而非向量表的 MSP（`CIMC_BootLoader/Function/bl_core.c:696-L709`）。
- **先 RAM 后 Flash**：payload 先累积到 `bl_official_payload_buf[16384]`，流空闲后才擦写下载区（`CIMC_BootLoader/Function/bl_core.c:69`、`:724-L734`、`:808-L823`）。这是为了避免 Flash 擦写停顿导致 USART 切片丢失，代价是接收上限受 16 KiB RAM 缓冲约束。
- **粘连帧处理**：上位机可能把 26 字节 ASCII 命令与首批裸 bin 拼在同一个 USART IDLE 包里，因此解析器只解码固定长度的命令前缀，把其后的非填充字节暂存并优先消费（`CIMC_BootLoader/Function/bl_core.c:463-L500`、`:608-L620`）。
- **错误帧延迟发送**：坏镜像在魔数关口就会被拒，但此时上位机可能还在发剩余字节，因此先排空到 100 ms 静默窗口再回 ERROR（`CIMC_BootLoader/Function/bl_core.c:737-L757`）。
- **超时参数**：单包空闲 1.5 s、整体 30 s、`0x0503` 等待 60 s（`CIMC_BootLoader/Function/bl_core.c:31-L34`）。

### 4.4 接收失败后的会话保持

`0x0502` 校验失败时不复位回 App，而是保持升级接收窗口并静默重等下一个 `0x0502`（`CIMC_BootLoader/Function/bl_core.c:892-L914`、`:1297-L1310`）。原因是官方评分流程在 N-02 发坏镜像、期望 ERROR，随后很快发正确镜像；若此间复位回 App，就会错过第二次 `0x0502`。这里记录的是当前实现策略，相关现象来自开发记忆中的评分日志复盘（`docs/dev-notes/PROJECT_MEMORY.md:265-L270`），本文不做实机复现声明。

## 5. 安装与回滚实现

### 5.1 `bl_install_downloaded_image()` 的校验与动作顺序

函数位于 `CIMC_BootLoader/Function/bl_core.c:1053-L1159`，顺序如下：

| 步骤 | 内容 | 依据 |
| --- | --- | --- |
| 0 | 计算 `erase_size = align_up(app_size, 4 KiB)`；为 0、或超过 `BL_APP1_SIZE`、或超过 `BL_APP_BACKUP_SIZE` 时直接判 `BL_ERR_APP2_INVALID` | `:1064-L1073` |
| 1a | 校验 `app_size != 0`、`app_size <= BL_APP1_SIZE`、`app_size <= BL_DOWNLOAD_SIZE`、下载区向量表合法 | `:1079-L1089` |
| 1b | 计算下载区 `[BL_DOWNLOAD_START_ADDR, +app_size)` 的 CRC32，与持久化的 `app_crc32` 比较 | `:1091-L1101` |
| 2 | 若 APP1 向量表当前有效，备份 APP1 的**擦除窗口**（`erase_size`）到备份区 | `:1107-L1110`、`:966-L977` |
| 3 | 从下载区复制 `erase_size` 字节到 APP1（擦除窗口后整窗编程） | `:1116-L1118`、`:986-L993` |
| 4a | 重算 APP1 前 `app_size` 字节的 CRC32；一致则置 `IDLE`、`update_counter++`、`last_error = BL_ERR_NONE` 并返回成功 | `:1124-L1134` |
| 4b | 不一致则置 `FAILED`、`fail_counter++`、`last_error = BL_ERR_COPY_FAILED`，并在 `app_backup_ready` 为真时尝试用备份恢复 | `:1136-L1147` |
| 4c | 步骤 2/3 本身失败时走同样的 `FAILED` 收尾并尝试恢复 | `:1149-L1158` |

两种区域搬运共用 `bl_copy_flash_region()`，使用 256 字节栈缓冲分块复制（`CIMC_BootLoader/Function/bl_core.c:925-L964`，缓冲大小定义于 `CIMC_App/common/bl_param.h:39`）。

### 5.2 备份与回滚的范围与限制

- **备份长度按新镜像对齐**：`bl_backup_app1(erase_size)` 只备份本次安装会擦除的 APP1 页窗口（`CIMC_BootLoader/Function/bl_core.c:966-L977`）。这样能把 `0x0503` OK 之后的静默 Flash 写入时间压到最小，但同时意味着**没有保留旧镜像的完整长度和独立 CRC**。
- **`app_backup_ready` 是 RAM 局部变量**：它只在本次 `bl_install_downloaded_image()` 调用内有意义（`CIMC_BootLoader/Function/bl_core.c:1055`、`:1109`、`:1139`、`:1152`）。函数返回、复位或掉电之后，BootLoader 无法知道备份区里是否有一份可用的旧镜像。当前也没有任何持久化的「安装阶段」或「备份有效」标记。
- **没有健康启动确认**：新镜像写入并通过 CRC 后就置 `IDLE`，不存在「新 App 主动回执成功、否则回滚」的机制。
- **备份区是整槽大小，但内容不保证长期有效**：备份区的容量与 APP1 相同（`CIMC_App/common/bl_partition.h:22-L24`），若上一次安装只备份了 8 KiB，备份区其余部分保持上一次的残留内容，没有元数据说明哪一段属于哪次备份。

因此当前实现的能力边界是：**支持本次调用内的失败恢复尝试**；不支持掉电重启后的自动回滚，也不支持恢复到「完整旧镜像」。

### 5.3 参数与日志落盘

安装完成后（无论成败）由 `bootloader_run()` 递增 `log_write_index`、写入 `BL_LOG_EVENT_UPDATE_OK`/`UPDATE_FAIL` 日志并整页提交（`CIMC_BootLoader/Function/bl_core.c:1337-L1350`）。日志条目带自身 CRC32（`CIMC_BootLoader/Function/bl_core.c:390-L402`），环形覆盖 32 条。安装与提交都被包在同一段关中断临界区里，提交结果 `commit_ok` 会参与最终错误判定（`:1344-L1349`）。

## 6. 路径 B：USART0 App 流式升级

### 6.1 状态机

| # | 状态 | 前置条件 | 动作 | 持久化内容 | 可能掉电的位置 |
| --- | --- | --- | --- | --- | --- |
| B1 | `OTA_STATE_WAIT_HEADER` | 服务初始化完成（`BootUpdate_Service_Init()` 清上下文、初始化 32 KiB 环形缓冲） | 每个升级任务周期检查环形缓冲；不足 4 字节直接返回 | 无 | — |
| B2 | 搜索包头 | 缓冲可用 ≥ 4 字节 | 先只比对 4 字节魔术字；不匹配则丢 1 字节重来 | 无 | — |
| B3 | 包头校验 | 缓冲可用 ≥ `sizeof(ota_stream_header_t)`（32 字节） | 深拷贝包头到局部变量，交 `OtaProtocol_IsStreamHeaderValid()` 校验；失败则丢 1 字节重新搜索 | 无 | — |
| B4 | 擦除下载区 | 包头校验通过 | `erase_size = align_up(app_size, 4 KiB)`，擦除 `BL_APP2_START_ADDR` 起该长度 | 无 | **擦除中掉电**：下载区内容不确定，且无任何持久标记，下次上电 BootLoader 按正常运行路径跳 App，旧 App 不受影响 |
| B5 | `OTA_STATE_RECV_STREAM` | 擦除成功 | 初始化 `crc_acc = 0xFFFFFFFF`、`recv_size = 0` | 无 | — |
| B6 | 消费数据 | 环形缓冲有数据 | 每次最多取 512 字节；写前检查 `recv_size + len <= expected_size`；写入 `BL_APP2_START_ADDR + recv_size`；累加 CRC、递增 `recv_size` | 无 | **传输中掉电**：下载区半写、无标记，重启后行为同 B4 |
| B7 | 收齐校验 | `recv_size == expected_size` | 检查 `rx_overflow`（溢出则拒绝）；`Crc32Finalize` 与包头期望值比较；不一致则打印错误并回 B1 | 无 | — |
| B8 | 提交待安装 | CRC 一致 | 缓存整页参数、修补/选用主备副本、置 `update_flag = PENDING`、写 `app_size`/`app_crc32`、清理 `last_error`、整页擦写 | `PENDING` + `app_size` + `app_crc32` | **提交中掉电**：整页擦除窗口内主备可能同时失效（见 11 节第 3 项） |
| B9 | 复位 | 提交成功 | 打印完成信息、延时 100 ms、`__disable_irq()`、`NVIC_SystemReset()` | 已在 B8 落盘 | 复位后 BootLoader 读到的 `update_flag` 为 `PENDING` |
| B10 | **（缺失）安装入口** | — | **BootLoader 当前没有 `PENDING` 分支**，`bootloader_run()` 只处理 `FAST_UPGRADE` 与 `DOWNLOADED` | — | 结果：设备停在 `PENDING` 状态并跳回旧 App，下载区里的新镜像永远不会被安装 |

### 6.2 涉及的具体实现

| 环节 | 实现 | 依据 |
| --- | --- | --- |
| 环形缓冲容量 | `OTA_RING_BUFFER_SIZE = 32 KiB` | `CIMC_App/Function/BootUpdate_service.c:13` |
| 写块上限 | `OTA_STREAM_WRITE_CHUNK = 512` | `CIMC_App/Function/BootUpdate_service.c:14` |
| 入口 | `Usart_Service_Task()` 取帧后调 `BootUpdate_Service_HandleFrame()`，后者写入环形缓冲并立即调用升级任务 | `CIMC_App/Function/Usart_service.c:57-L59`、`CIMC_App/Function/BootUpdate_service.c:459-L472` |
| USART0 配置 | DMA 模式、115200、RX/帧缓冲 32768 | `CIMC_App/Driver/USART/USART.h:23`、`:42-L43`、`CIMC_App/Function/Usart_service.c:51` |
| 包头结构 | 32 字节 packed，含魔术字/包头大小/包头版本/App 版本/大小/CRC32/目标地址/镜像类型/包头 CRC32 | `CIMC_App/Protocol/ota_protocol.h:30-L49` |
| 包头校验项 | 魔术字、包头版本、包头大小、目标地址 `== BL_APP1_START_ADDR`、镜像类型、`0 < app_size <= BL_APP2_SIZE`、包头 CRC32 | `CIMC_App/Protocol/ota_protocol.c:15-L28` |
| 缓冲溢出的处理 | `ring_buffer_write()` 失败置 `rx_overflow`，finalize 阶段拒绝成功 | `CIMC_App/Function/BootUpdate_service.c:459-L464`、`:310-L314` |
| 参数提交 | `param_commit_update()`：选有效副本（主优先，无计数器比较）、置 `PENDING`、整页擦写 | `CIMC_App/Function/BootUpdate_service.c:167-L206` |

### 6.3 这条路径为什么尚未闭环

1. `bl_core.c` 中不存在任何对 `BL_UPDATE_FLAG_PENDING` 的读取或消费分支。`bootloader_run()` 判断的两个状态是 `FAST_UPGRADE`（`CIMC_BootLoader/Function/bl_core.c:1280`）与 `DOWNLOADED`（`:1318`）。在 `PENDING` 状态下，两个分支都不成立，代码会直接走到末尾的 `bl_is_app_vector_valid(BL_APP1_START_ADDR)` 判断（`:1353`）并跳回旧 App。
2. 进入 `PENDING` 后 `update_flag` 不会被自动清除，因此这个状态会一直留在参数页里，直到下一次 `0x0501` 把它覆盖成 `FAST_UPGRADE`，或某个安装流程把它写成 `IDLE`/`FAILED`。旁路影响：任何读 `update_flag` 的外部工具都会看到一个「非 IDLE 但无对应处理」的状态。
3. 因此第 6.1 节状态机中 B10 是缺的。这条路径目前具备的能力是：接收、落盘到下载暂存区、CRC 校验、持久化元数据；**不具备的能力是：安装**。在文档和对外说明中不能把两边函数都已存在表述为「App 流式升级已闭环」。

## 7. 跳转 App 的完整序列

`bl_jump_to_app(app_base)`（`CIMC_BootLoader/Function/bl_core.c:1191-L1225`）按以下顺序执行：

| 步骤 | 动作 | 目的 | 依据 |
| --- | --- | --- | --- |
| 1 | `bl_release_uart_before_app_jump()`：关 `USART_INT_RBNE`/`USART_INT_IDLE`、`NVIC_DisableIRQ(USART_IRQn)`、`NVIC_ClearPendingIRQ(USART_IRQn)`、拉低 RS485 DE、关收发、`usart_disable()` | 把 BootLoader 占用的 USART1/RS485 资源交还，避免残留中断进入新 App，也避免 DE 仍拉高导致 App 首个心跳被电气遮蔽 | `:1161-L1189` |
| 2 | `__disable_irq()` | 后续序列在关中断下完成 | `:1203` |
| 3 | `SysTick->CTRL/LOAD/VAL = 0` | 停掉 BootLoader 的 SysTick，防止其异常在 App 向量表上取指 | `:1205-L1207` |
| 4 | 遍历 `NVIC->ICER[0..7]` 与 `NVIC->ICPR[0..7]` 写全 1 | 清掉所有使能位与挂起位 | `:1209-L1212` |
| 5 | `__DSB()` + `__ISB()` | 保证前述系统寄存器写入在后续取指前完成 | `:1214-L1215` |
| 6 | `SCB->VTOR = app_base` | 让后续异常使用 App 向量表 | `:1217` |
| 7 | `__set_MSP(*(volatile uint32_t *)app_base)` | 从 App 向量表首字加载主栈指针 | `:1218` |
| 8 | 取 `*(app_base + 4)` 作为复位入口并强转为函数指针 | 从 App 向量表第二字取 Reset_Handler | `:1220-L1221` |
| 9 | `__enable_irq()` 后调用入口 | 把控制权交给 App | `:1223-L1224` |

### 7.1 为什么只改 PC 不够

- **栈没换**：BootLoader 的调用栈和 MSP 仍指向 BootLoader 的栈布局。App 从自己的 `System_Init()` 开始重新配置时钟与外设，如果还跑在 BootLoader 的栈上，栈深度和 `__initial_sp` 语义都不再成立。
- **中断没换**：异常入口地址由 `VTOR` 决定。不改 `VTOR` 时，App 运行期间发生的任何中断都会跳到 BootLoader 的向量表，而该向量表指向 BootLoader 的 ISR，其中引用的外设状态可能已被 App 重配置。
- **挂起中断会立即触发**：NVIC 中残留的 pending 位（例如 USART1 的 IDLE）会在开中断瞬间进入处理器。BootLoader 的向量代码位于内部 Flash，与 App 运行在同一片 Flash 上——这是必须在开中断前清挂起位与使能位的原因。
- **SysTick 继续计数**：BootLoader 已配置的 SysTick 会持续产生异常并调用 BootLoader 的 handler。

### 7.2 入口合法性检查的宽严

跳转前的检查由 `bl_is_app_vector_valid()` 完成（`CIMC_BootLoader/Function/bl_core.c:242-L263`），只检查两项：

| 检查项 | 判据 | 宽松之处 |
| --- | --- | --- |
| 初始 MSP | 落在 `[0x20000000, 0x20040000)` | 范围覆盖整个 SRAM 窗口，不校验栈顶方向、不校验是否与 App 实际链接的栈顶一致 |
| Reset Handler | `[BL_FLASH_BASE_ADDR, BL_FLASH_END_ADDR]` | **整片 Flash 都接受**：下载区地址、备份区地址、甚至 BootLoader 自身地址都会通过；未检查 Thumb 位（bit0），未限定必须落在 `BL_APP1_START_ADDR` 起 `app_size` 范围内 |

RAM 暂存镜像在写入下载区之前用的是另一份等价检查 `bl_is_ram_vector_valid()`（`CIMC_BootLoader/Function/bl_core.c:663-L684`），判据相同，区别只是从 RAM 缓冲拷贝出来比较。它同样是宽检查，主要作用是拦掉全 `0xFF` 的空 Flash。一个具体后果是：若下载区里是一份链接地址为其它区间的合法 Cortex-M 镜像，向量检查会通过，CRC 校验（针对持久化的 `app_size`/`app_crc32`）也可能通过，但跳转后 App 的运行地址与自身链接地址不符。

## 8. CRC 与完整性

### 8.1 CRC32 参数

| 项目 | 取值 | 依据 |
| --- | --- | --- |
| 多项式（反射形式） | `0xEDB88320` | `CIMC_App/Protocol/cimc_protocol.c:308` |
| 初值 | `0xFFFFFFFF` | `CIMC_App/Function/BootUpdate_service.c:269`、`CIMC_BootLoader/Function/bl_core.c:767`、`CIMC_App/Protocol/cimc_protocol.c:327` |
| 输入反射 | 是（逐字节 `crc ^= data[i]` 后右移） | `CIMC_App/Protocol/cimc_protocol.c:301-L315` |
| 输出反射 | 是 | 同上 |
| 最终异或 | `0xFFFFFFFF` | `CIMC_App/Protocol/cimc_protocol.c:320-L323` |

这与标准 CRC-32（zlib / `binascii.crc32`）等价。打包脚本用 Python `binascii.crc32`（`CIMC_App/Tools/ota_stream_pack.py:50-L51`），固件用上述实现，两侧一致。

### 8.2 覆盖范围

| 被保护对象 | 覆盖范围 | 排除 | 依据 |
| --- | --- | --- | --- |
| `bl_param_t` | `[0, offsetof(param_crc32))` = 前 272 字节（含 `reserved[]` 与其中的 App 业务块） | `param_crc32` 自身、`tail_magic` | `CIMC_BootLoader/Function/bl_core.c:87-L90` |
| App 业务参数块 | `[0, offsetof(crc32))` = 前 60 字节 | 块内 `crc32` 字段 | `CIMC_App/Function/App_param_service.c:38-L41` |
| BootLoader 日志条目 | `[0, offsetof(crc32))` = 前 28 字节 | `crc32` 字段 | `CIMC_BootLoader/Function/bl_core.c:401` |
| OTA 包头 | `[0, offsetof(header_crc32))` = 前 28 字节 | `header_crc32` 字段 | `CIMC_App/Protocol/ota_protocol.c:26-L28` |
| 镜像 payload（流式路径） | 实际接收的 `app_size` 字节 | — | `CIMC_App/Function/BootUpdate_service.c:296`、`:316-L322` |
| 镜像 payload（官方路径） | 剥离魔术字后的 payload 字节 | 4 字节魔术字、页对齐补的 `0xFF` | `CIMC_BootLoader/Function/bl_core.c:730-L732` |
| 安装后复核 | APP1 前 `app_size` 字节 | 擦除窗口尾部 | `CIMC_BootLoader/Function/bl_core.c:1125` |

注意最后两行：下载区与 APP1 都是**按页窗整窗编程**（`erase_size`），但 CRC 只覆盖 `app_size` 字节。因此擦除窗口尾部的 `0xFF` 补位不参与校验——这是刻意的（尾部内容由链接产物决定），但也意味着尾部若被写坏，CRC 发现不了。

### 8.3 CRC 能力的边界

- CRC32 是**检错**手段，不是**认证**手段。它只能发现随机错误（位翻转、截断、Flash 编程失败），不能区分「上位机发的正确镜像」和「攻击者构造的、CRC 自洽的镜像」。
- 官方路径的 CRC 是 BootLoader 自己累计 payload 得到并持久化的（`CIMC_BootLoader/Function/bl_core.c:1291`），随后安装前把下载区重算值与这个持久值比较（`:1091-L1101`）。这一步主要证明「下载区内容与当初收到的内容一致」，即内部搬运一致性；它**不能**证明当初收到的内容就是发送者期望的完整镜像，因为期望值来自接收方自己。
- 流式路径有外部期望值：包头里的 `app_crc32` 来自打包工具计算的原始 `App.bin`（`CIMC_App/Tools/ota_stream_pack.py:91`），finalize 阶段与之比较（`CIMC_App/Function/BootUpdate_service.c:316-L322`）。但包头本身只有 CRC32 保护，不具备防篡改能力。
- 两条路径都**没有**签名、没有安全启动、没有防回退（版本号比较）。`ota_package_config.h` 里的 `OTA_PACKAGE_APP_VERSION` 只用于打包时填写，BootLoader 侧不读取也不比较它。

### 8.4 不要与 CRC16-Modbus 混淆

RS485 协议帧的校验用的是 CRC16-Modbus：反射多项式 `0xA001`、初值 `0xFFFF`，覆盖解码后二进制帧的前 `9 + content_len` 字节，线上以大端字段写出（`CIMC_App/Protocol/cimc_protocol.c:31`、`:190`、`:249`）。它与 OTA 的 CRC32 是两套独立算法，作用层级也不同：

| | CRC16-Modbus | OTA CRC32 |
| --- | --- | --- |
| 作用对象 | 一帧 ASCII-Hex 命令/应答帧 | 整个固件镜像、参数块、包头 |
| 长度 | 16 位 | 32 位 |
| 校验通过代表 | 帧在链路上没坏 | 镜像/元数据内容完整 |

`0x0502`/`0x0503` 命令帧本身走 CRC16-Modbus，bin 数据走 CRC32，二者在同一会话中同时存在，不能互相替代。

---

## 9. 工具链

### 9.1 `ota_stream_pack.py`

位置：`CIMC_App/Tools/ota_stream_pack.py`。作用是把 Keil 产出的裸 `App.bin` 打包成「32 字节包头 + 裸 bin」的流式 OTA 文件。

处理流程：

1. 读 `--config` 指定的头文件（默认 `CIMC_App/common/ota_package_config.h`），用正则提取所有 `OTA_PACKAGE_*` 宏并按 C 整数字面量解析（`:19-L43`）。缺少必需宏时直接报错退出（`:31-L41`）。
2. 读入 `--bin`，检查非空、且 `app_size <= OTA_PACKAGE_MAX_SIZE`（默认 128 KiB）（`:72-L76`）。
3. 校验原始镜像向量表：首字（初始 MSP）必须在 `[0x20000000, 0x20040000)`，第二字（复位入口）高字节必须为 `0x08`（即位于 Flash 区），否则报错退出（`:78-L82`）。
4. 用 `struct.pack("<IHHIIIII", …)` 生成 28 字节包头体：魔术字（默认 `0x12345678`）、包头大小固定 `32`、包头版本（3）、App 版本、`app_size`、镜像 CRC32、目标地址（默认 `0x08011000`）、镜像类型（1）（`:84-L94`）。
5. 计算包头 CRC32 并追加为第 29–32 字节（`:95-L96`）。
6. 按 `[包头][裸 bin]` 顺序写出，包头的 `app_size`/`app_crc32`/`header_crc32` 均由脚本计算填入；`ota_package_config.h:12-L16` 的注释里把这三个字段描述为「filled by pack tool」（`:101-L107`）。

命令行示例（默认参数即可，`--config` 可省略）：

```bat
python CIMC_App\Tools\ota_stream_pack.py ^
  --bin CIMC_App\project\Objects\CIMC_App.bin ^
  --out CIMC_App\project\Objects\CIMC_App_ota.bin
```

可覆盖的选项：

```bat
python CIMC_App\Tools\ota_stream_pack.py ^
  --bin CIMC_App\project\Objects\CIMC_App.bin ^
  --out build\CIMC_App_ota_v2.bin ^
  --config CIMC_App\common\ota_package_config.h ^
  --version 0x00020000 ^
  --target-addr 0x08011000 ^
  --max-size 0x00020000
```

`--version`/`--target-addr`/`--max-size` 会覆盖头文件中的默认值，接受 `0x` 前缀或十进制。成功时打印版本、大小、镜像 CRC 与包头 CRC（`:105-L107`）。

### 9.2 `make_ota_package.bat`

位置：`CIMC_App/project/make_ota_package.bat`。它是上述命令的固定参数包装：

| 变量 | 取值 | 依据 |
| --- | --- | --- |
| `PYTHON` | `python`（取自 PATH） | `:5` |
| `PACKER` | `..\Tools\ota_stream_pack.py` | `:6` |
| `CONFIG` | `..\common\ota_package_config.h` | `:7` |
| `APP_BIN` | `.\Objects\CIMC_App.bin` | `:8` |
| `OTA_BIN` | `.\Objects\CIMC_App_ota.bin` | `:9` |

脚本先 `cd /d "%~dp0"` 切到自身所在目录（`:3`），因此必须在 App 工程编译产出 `.bin` 之后调用，否则会因为找不到 `Objects\CIMC_App.bin` 而报 `[OTA] App bin not found` 并以退出码 1 结束（`:16-L20`、`:29-L31`）。打包成功打印 `[OTA] Done: <路径>` 并返回 0。

用法：

```bat
cd CIMC_App\project
make_ota_package.bat
```

该脚本产出的是**流式路径**的包（带 `ota_stream_header_t` 包头）。官方 RS485 路径使用的是另一份以 `5A A5 C3 3C` 开头的镜像，不由本脚本生成。

## 10. 验证手段

### 10.1 脚本性质说明

`tools/` 下的这批 `.ps1` 都是**静态源码守护脚本**：用正则断言源码中必须存在（或必须不存在）某些写法。它们不编译、不烧录、不驱动串口，因此通过只说明「实现没有被改回已知错误形态」，**不构成功能或掉电安全验收**。所有脚本都已改为用 `Split-Path -Parent $PSScriptRoot` 从脚本自身位置推导工程根（例如 `tools/verify_t12_bootloader_receive_bin.ps1:29`），所以可以在任意工作目录直接运行。

通用运行方式：

```powershell
powershell -File tools/verify_n03_bootloader_app_handoff.ps1
```

或一次性跑完本文相关脚本：

```powershell
$root = 'tools'
foreach ($s in 'verify_t12_bootloader_receive_bin.ps1',
               'verify_t13_bootloader_install.ps1',
               'verify_t14_official_sequence.ps1',
               'verify_n02_bad_firmware_error_timing.ps1',
               'verify_n03_bootloader_app_handoff.ps1',
               'verify_n_series_bootloader_stream_rx.ps1') {
    powershell -File (Join-Path $root $s)
}
```

每个脚本成功时打印一行 `... static verification passed.`，失败时抛异常并以非零码退出。

### 10.2 各脚本的断言内容

| 脚本 | 断言的要点 |
| --- | --- |
| `tools/verify_t12_bootloader_receive_bin.ps1` | App 与 BootLoader 两侧 `bl_param.h` 都必须定义 `BL_UPDATE_FLAG_DOWNLOADED = 0xA5A55A5A`；BootLoader USART 接收缓冲 ≥ 512 字节且 `recv_len` 为 `uint16_t`、ISR 有边界检查；`bl_core.c` 必须有 4 字节魔术字常量、16 KiB RAM 暂存缓冲、`bl_receive_official_firmware`/`bl_append_official_payload_to_ram`；粘连帧处理相关的 `BL_ASCII_EMPTY_COMMAND_FRAME_LEN`、`bl_stage_glued_raw_bytes`、`s_glued_raw_pending`、`bl_consume_glued_raw_bytes` 必须存在；接收成功后必须持久化 `DOWNLOADED` + payload 长度 + payload CRC 并回 OK，失败必须回 ERROR 且**不得**复位回 App；另外检查 `docs/` 下计划文档含 T12 小节、人工核验清单含 `0x0502` 帧 `A5B600010105020002255AB6A5` |
| `tools/verify_t13_bootloader_install.ps1` | 分区常量 `BL_APP_BACKUP_START_ADDR = 0x08031000`、`BL_DOWNLOAD_START_ADDR = 0x08051000`；`DOWNLOADED`/`FAILED` 状态值；`0x0503` 解析器与有界等待窗口；`bl_send_ok_frame(0x0503U)` 必须早于破坏性搬运、失败路径必须回 ERROR；下载区整窗编程与尾部 `0xFF` 补位；`bl_install_downloaded_image` 的校验顺序（按页对齐窗口、安装前验下载区 CRC、安装后验 APP1 CRC）；备份与回滚都必须按页窗口长度，**禁止**整槽备份/整槽回滚；成功写 `IDLE` + `UPDATE_OK` 日志，失败写 `FAILED` + `UPDATE_FAIL` 日志；`0x0503` ACK 后必须先 `__disable_irq()` 再安装与提交；跳转前必须停 SysTick、清 NVIC 使能与挂起、设 `VTOR`、加载 MSP |
| `tools/verify_t14_official_sequence.ps1` | BootLoader 必须从 `bl_param_t.reserved[32]` 读取 App 参数块并校验魔术字 `0x43494D43`、版本 `0x00020000`、CRC；App 与 BootLoader 两侧参数块都必须含 `dac_raw` 以保持布局一致；BootLoader 必须派生运行期设备 ID 与波特率；官方 OTA 波特率必须为 115200（禁止硬编码回 19200），USART 初始化必须用 `bootloader_get_upgrade_baudrate()` 的返回值；`0x0502`/`0x0503` 解析必须接受当前持久化 ID 或广播，OK/ERROR 帧**禁止**硬编码 `0x0001`；另外检查计划文档与 PROJECT_MEMORY 记录了 L/M/N 连续顺序决策 |
| `tools/verify_n02_bad_firmware_error_timing.ps1` | `BL_OFFICIAL_BIN_ERROR_QUIET_MS = 100`；必须存在 `bl_drain_official_stream_until_quiet()` 且用静默窗口（而非固定盲延时）作为条件；魔数校验失败的**两条**路径（粘连字节路径与逐字节路径）都必须先排空再返回失败 |
| `tools/verify_n03_bootloader_app_handoff.ps1` | 跳转前必须调用 `bl_release_uart_before_app_jump()`，且该调用必须紧接在 `__disable_irq()` 之前；清理内容必须包含关 RBNE/IDLE 中断、禁 USART1 NVIC 线、清其挂起位、拉低 RS485 DE（PE8）、关 USART1 收发并使能位；USART 映射必须仍是 `USART1`/`USART1_IRQn`/`GPIOE`/`GPIO_PIN_8` |
| `tools/verify_n_series_bootloader_stream_rx.ps1` | BootLoader USART 必须有 8192 字节接收环与 `volatile` 头尾指针；ISR 必须每字节只读一次数据寄存器并压入环；必须暴露 `bl_usart_read_byte()` 与 `bl_usart_clear_rx_stream()`；`bl_core.c` 必须以字节流方式消费（`while(bl_read_uart_byte(&ch))`），进入升级窗口前必须清流；等待窗口内 `system init`/版本字样必须重复打印（`remaining >= 6U`） |

### 10.3 这些脚本没有覆盖的内容

- 不验证 Flash 擦写时序、不验证中断关闭窗口的实际时长。
- 不验证参数页擦除过程中掉电的行为。
- 不验证 16 KiB RAM 暂存上限对应的实际镜像尺寸，也不验证流式路径 32 KiB 环形缓冲的容量边界。
- 不验证 `PENDING` 分支是否存在（因为当前设计上就不存在）。
- `verify_t12`/`verify_t13`/`verify_t14` 还会读取 `docs/` 下的计划文档、`docs/verification/RS485人工核验清单.md` 和 `PROJECT_MEMORY.md`；如果这些文档被移动或改名，脚本会因找不到文件而失败，这与代码正确性无关。

## 11. 已知边界与待办

以下 14 项均为当前代码的实际状态，按影响面排序。

| # | 缺口 | 说明 | 现状 |
| --- | --- | --- | --- |
| 1 | **`PENDING` 安装入口缺失** | App 流式路径在 CRC 通过后写 `PENDING` 并复位，但 `bootloader_run()` 只处理 `FAST_UPGRADE` 与 `DOWNLOADED`，没有 `PENDING` 分支（`CIMC_BootLoader/Function/bl_core.c:1280`、`:1318`）。设备会带着 `PENDING` 跳回旧 App，下载区镜像永不安装 | 未实现 |
| 2 | **备份元数据不足** | 备份区没有保存旧镜像的完整长度与独立 CRC，备份长度按新镜像的 `erase_size` 决定（`CIMC_BootLoader/Function/bl_core.c:966-L977`）。新镜像小于旧镜像时，无法据此恢复完整旧 App | 设计缺陷 |
| 3 | **参数主备同页擦除** | 主副本 `+0x000`、备副本 `+0x100` 在同一 4 KiB 页内（`CIMC_App/common/bl_param.h:11-L12`），任何提交都是整页擦除后重写（`CIMC_BootLoader/Function/bl_core.c:337-L342`）。擦除窗口内掉电可能让两份同时失效，此时只能恢复默认参数。主备是内容冗余，不是跨擦除单元的掉电保障 | 已知风险，未验收 |
| 4 | **写入未回读验证** | App 侧 `InternalFlash_WriteBytes()` 逐字节编程后不读回比较，也不检查 `fmc_byte_program()` 的错误标志（`CIMC_App/Driver/InternalFlash/InternalFlash.c:41-L47`）；`AppParam_Service_Commit()` 直接返回 1，不检查写入结果（`CIMC_App/Function/App_param_service.c:222-L225`）。RS485 的 OK 应答不能等同于持久化成功 | 未实现，未验收 |
| 5 | **掉电点未做全覆盖回归** | 第 4 节与第 6 节状态机中标注的破坏性窗口（参数页擦除、下载区擦除、搬运、备份、提交）都没有逐点切电的回归记录。本文对这些位置的描述属于代码推导，不是实测结论 | 未验收 |
| 6 | **无签名、无安全启动、无防回退** | 完整性只靠 CRC32（见 8.3）。`OTA_PACKAGE_APP_VERSION` 只用于打包，BootLoader 不读取、不比较版本，因此不能阻止刷入更旧或非授权镜像 | 未实现 |
| 7 | **向量表校验偏宽** | `bl_is_app_vector_valid()` 只要求 MSP 落在 SRAM 区间、Reset Handler 落在整片 Flash 区间（`CIMC_BootLoader/Function/bl_core.c:242-L263`），不检查 Thumb 位、不限定必须落在 APP1 范围内；RAM 暂存检查同样宽（`:663-L684`） | 偏宽，可收紧 |
| 8 | **App 与 BootLoader 的参数选择规则不一致** | BootLoader 在两份都有效时按 `update_counter` 取大（`CIMC_BootLoader/Function/bl_core.c:1250-L1256`），App 直接取主（`CIMC_App/Function/App_param_service.c:176-L185`）。两侧可能对同一参数页得出不同结论 | 不一致 |
| 9 | **App 侧默认参数块未算 CRC** | 两份都失效时构造的默认块只有魔数/版本/地址/尾魔数，`param_crc32` 为 0（`CIMC_App/Function/App_param_service.c:187-L193`）。当前只作为 Commit 的输入基底因而无直接危害，但缺少保护 | 待收敛 |
| 10 | **16 KiB RAM 暂存上限** | 官方路径把 payload 暂存在 16 KiB 的 `bl_official_payload_buf`（`CIMC_BootLoader/Function/bl_core.c:28`、`:720-L722`），超过即拒收。下载区虽然标称 128 KiB，但当前官方路径的实际接收上限由这块 RAM 决定 | 容量约束 |
| 11 | **流式路径容量与实测边界未验证** | 包头校验只限制 `app_size <= BL_APP2_SIZE`（128 KiB），而 App 侧环形缓冲为 32 KiB（`CIMC_App/Function/BootUpdate_service.c:13`）。边收边写理论上可以处理更大的镜像，但缓冲区追赶能力、实际可用速率和溢出边界都没有量测记录 | 未验收 |
| 12 | **页窗尾部不受 CRC 保护** | 下载区与 APP1 都按 `erase_size` 整窗编程，但 CRC 只覆盖 `app_size` 字节。擦除窗口尾部的 `0xFF` 补位若被写坏，校验发现不了 | 已知局限 |
| 13 | **`0x0503` 安装窗口无输出** | `0x0503` OK 之后到参数提交完成之间关中断执行 Flash 操作，期间没有串口输出。总线侧只能观察到一段静默，无法从中区分「正在安装」和「已卡死」 | 可观测性缺口 |
| 14 | **无健康启动确认** | 新镜像通过 CRC 即置 `IDLE` 并跳转，不存在「新 App 启动成功并回执，否则回滚」的机制；也没有启动失败的计数与自动降级 | 未实现 |

## 12. 依据

本文实际读取并据此撰写的文件（路径均相对工程根目录）：

- 分区与共享参数：`CIMC_App/common/bl_partition.h`、`CIMC_App/common/bl_param.h`、`CIMC_BootLoader/common/bl_param.h`、`CIMC_App/common/ota_package_config.h`
- 协议与 CRC：`CIMC_App/Protocol/cimc_protocol.h`、`CIMC_App/Protocol/cimc_protocol.c`、`CIMC_App/Protocol/ota_protocol.h`、`CIMC_App/Protocol/ota_protocol.c`
- App 侧实现：`CIMC_App/Function/BootUpdate_service.c`、`CIMC_App/Function/App_param_service.c`、`CIMC_App/Function/Rs485_service.c`、`CIMC_App/Driver/InternalFlash/InternalFlash.c`、`CIMC_App/Driver/USART/USART.h`
- BootLoader 侧实现：`CIMC_BootLoader/Function/bl_core.c`、`CIMC_BootLoader/Function/bl_core.h`、`CIMC_BootLoader/Function/bl_config.h`
- 打包工具：`CIMC_App/Tools/ota_stream_pack.py`、`CIMC_App/project/make_ota_package.bat`
- 静态验证脚本：`tools/verify_t12_bootloader_receive_bin.ps1`、`tools/verify_t13_bootloader_install.ps1`、`tools/verify_t14_official_sequence.ps1`、`tools/verify_n02_bad_firmware_error_timing.ps1`、`tools/verify_n03_bootloader_app_handoff.ps1`、`tools/verify_n_series_bootloader_stream_rx.ps1`
- 交叉核对：`docs/dev-notes/PROJECT_MEMORY.md`（开发记忆，仅用于参考现象与决策来源，与源码冲突时以源码为准）；另参考了工程外的作者问答稿 `工业采集终端.md`（位于素材库，非本仓库文件），其中标注为「设计推演/改造方案/未实现/待核实」的内容在本文中一律按未实现处理。
