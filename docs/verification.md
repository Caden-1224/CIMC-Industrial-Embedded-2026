# 验证方法与证据

本文说明本工程用什么手段保证正确性、这些手段各自能证明什么、以及哪些结论至今没有证据支撑。文中所有行号引用均为相对工程根目录的源码位置，且在撰写时逐条核对过；凡未做过实机或仪器测量的结论，一律标注为「未做」或「待补」。

## 1. 验证策略总览

工程采用四层互补的验证手段，按「能证明的强度」由弱到强排列：

1. **协议层单元级断言**（人工，非自动化）。`CIMC_App/Protocol/cimc_protocol.c` 与 `CIMC_BootLoader/Protocol/cimc_protocol.c` 承担 ASCII-Hex 编解码、帧结构校验、CRC-16-Modbus、CRC32 与浮点大端读写。这部分不依赖硬件，但工程内没有单元测试框架，验证方式是人工核验清单里的解析器用例（见第 5 节）加上两端协议层共用同一份实现来减少分歧。
2. **BootLoader 静态守护断言**。`tools/` 下的 PowerShell 脚本对 BootLoader 的收包、校验、安装、跳转序列做正则断言。这类断言的价值在于「防止已经修对的地方被改回去」，例如跳转 APP 前必须释放 USART1 与 RS485 方向控制（`CIMC_BootLoader/Function/bl_core.c:1161-L1189`）。
3. **App 业务静态守护断言**。同一批脚本对参数持久化路径、数据面换算入口、DAC 联动、自动上报状态机、告警与睡眠路径做断言。断言口径大多落在「某字段必须存在」「某命令字必须被分发」「某危险写法必须不出现」三件事上。
4. **人工核验清单 + 上位机联调**。`docs/verification/RS485人工核验清单.md` 与 `docs/verification/协议人工核验清单.md` 记录真实的串口收发帧、观测点与实测结论，是本工程唯一带有「实机做过」性质的材料。

### 1.1 这些手段共同覆盖了什么

- 协议层：帧头帧尾、长度字段、协议版本、CRC16 的校验路径；CRC32 与浮点大端读写；ASCII-Hex 的输入规范化与拒绝条件。
- App 业务面：系统管理命令、业务参数持久化、三通道数据换算入口、DAC 输出范围检查、自动上报状态机、告警记录、睡眠唤醒、OLED/LED 状态。
- 升级链路：`0x0501` 进入 BootLoader、`0x0502` 接收官方裸 bin、`0x0503` 安装与回滚、跳转 APP 前的收尾。
- Flash 布局：BootLoader、参数页、App1、App 备份区、下载缓存五段的地址与相邻性由编译期断言保护（`CIMC_BootLoader/common/bl_partition.h:47-L65`）。

### 1.2 局限（必须先说清楚）

- **大部分脚本是源码/工程文件的静态断言，不等同硬件在环测试**。脚本读取 `.c/.h/.uvprojx` 文本后做正则匹配，不编译、不烧录、不驱动串口、不看波形。脚本通过只能说明「实现形态没有被改回已知错误形态」，不能说明功能正确，更不能说明时序正确。
- **没有自动化测试框架**。工程内没有单元测试、没有断言库、没有测试夹具；协议层与业务层的重复回归完全依赖上面这批脚本和人工核验。
- **没有 CI**。没有任何流水线配置，脚本需要有人在 Windows 上手动执行，因此「最后一次全部通过」这个事实本身也不会被持续保证。
- **构建验证是手动做的，且只有一次记录**。2026-10-03 在 Keil MDK V5.41（ARMCC V5.06 update 7）下对两个工程各做了一次全量重编译，结果见 1.3 节；仓库本身没有构建流水线，这次结果不会被持续复现。
- **没有掉电回归报告**。参数页擦除、下载区擦除、搬运、备份、参数提交这几个破坏性窗口都没有逐点切电的记录；`docs/ota.md` 第 11 节已把该项标为「未验收」。
- **没有硬件测量数据**。低功耗电流、电源板带载与温升、纹波与效率、EMC/浪涌、采样精度标定均无实测报告。
- **脚本自身有前置文件依赖**，缺文件时脚本会失败而脚本失败与代码正确性无关（详见第 2.5 节）。

### 1.3 构建验证（2026-10-03 实测）

用 Keil MDK V5.41 的命令行（`UV4.exe -r <工程> -j0 -o <日志>`）对两个工程各做一次**全量重编译**，不是增量构建：

| 工程 | 重写目标文件 | 结果 | Program Size | 与 2026-06-06 最后一次构建对比 |
| --- | --- | --- | --- | --- |
| `CIMC_BootLoader/project/CIMC_BootLoader.uvprojx` | 43 / 43 | `0 Error(s), 0 Warning(s)` | Code=10614 RO-data=802 RW-data=48 ZI-data=63024 | 数字完全一致 |
| `CIMC_App/project/CIMC_App.uvprojx` | 73 / 74 | `0 Error(s), 0 Warning(s)` | Code=31326 RO-data=8678 RW-data=308 ZI-data=150804 | 数字完全一致 |

编译工具链为 ARMCC V5.06 update 7（`E:\Keil_v5\ARM\ARMCC\Bin`），器件 GD32F470VE，App 链接在 `0x08011000`、BootLoader 在 `0x08000000`。

同一轮还跑通了 OTA 打包链路（`CIMC_App/project/make_ota_package.bat` → `Tools/ota_stream_pack.py`）：

```
OTA package: .\Objects\CIMC_App_ota.bin
  version=0x00010000 size=40092 app_crc=0x96EAA6B0 header_crc=0xE09F44D7
```

注意两点边界：**编译通过只说明源码能被工具链接受**，不代表功能、时序或掉电行为正确（上面 1.2 节的局限仍然成立）；另外 OTA 包头里的 `app_version` 取自 `CIMC_App/common/ota_package_config.h` 的 `OTA_PACKAGE_APP_VERSION`（`0x00010000`），与固件自身通过 `0x0104` 上报的版本宏 `2.0.1.0`（`CIMC_App/Function/App_param_service.h:6-L9`）是两处独立常量，当前并不联动。

## 2. 验证脚本清单

`tools/` 下共 17 个 `.ps1`。全部为只读脚本，不改动工程文件，成功时打印一行 `... passed.`，失败时打印原因或以非零码退出。

### 2.1 通用运行方式

脚本已统一改为按自身位置推导工程根目录（`Split-Path -Parent $PSScriptRoot`），因此可以在任意工作目录执行：

```powershell
powershell -File tools/verify_t12_bootloader_receive_bin.ps1
```

支持参数的脚本（App 侧的前 6 个）额外提供 `-ProjectRoot`：

| 脚本 | 参数 | 默认值 |
| --- | --- | --- |
| `verify_app_dac_migration.ps1` | `-ProjectRoot` | 脚本上一级目录 |
| `verify_app_t03_system_commands.ps1` | `-ProjectRoot` | 同上 |
| `verify_app_t04_t05_data_params.ps1` | `-ProjectRoot` | 同上 |
| `verify_app_t06_dac_linkage.ps1` | `-ProjectRoot` | 同上 |
| `verify_app_three_channel_mapping.ps1` | `-ProjectRoot` | 同上 |
| `verify_boot_migration.ps1` | `-Root` | 同上 |

其余 11 个脚本无参数，工程根目录固定由脚本位置推导。注意 BootLoader 侧脚本用 `Join-Path $root 'CIMC_BootLoader\...'` 拼接路径，因此从 `tools/` 直接运行即可，不要单独把脚本复制到别处。

### 2.2 App 业务（9 个）

| 脚本 | 所属阶段 / 主题 | 断言要点 | 运行方式 |
| --- | --- | --- | --- |
| `verify_app_dac_migration.ps1` | 驱动分层迁移：DAC 从旧目录迁到 `Driver/DAC` | 1) `DAC.h` 暴露 `DAC_Init` / `DAC_SetRaw`；2) `DAC.c` 把 PA4 配成模拟模式并写 12 位右对齐数据；3) `Dac_service` 的服务层接口存在且被 `HeaderFiles.h`、`Function.h` 收录；4) `System_Init` 调用 `Dac_Service_Init()`；5) Keil 工程含 `DAC.c`、`Dac_service.c` 与 `Driver\DAC` 包含路径 | `powershell -File tools/verify_app_dac_migration.ps1` |
| `verify_app_t03_system_commands.ps1` | T03 系统管理命令与设备身份 | 1) 默认 ID/波特率/固件版本宏被固定为 `0x0001`/`0x13`/`2.0.1.0`；2) `0x0101`、`0x0104`、`0x0105`、`0x0106`、`0x01A1`、`0x0111`、`0x01A2`、`0x0112` 均被分发；3) 参数校验包含 BootLoader 参数页 CRC；4) 提交走整页擦写并同步备份副本；5) 出现 `bl_commit_param(` 即失败（App 不得链接 BootLoader 私有接口）；6) 分级复位延迟 `300ms` / `700ms` 与启动心跳只在任务里发 | `powershell -File tools/verify_app_t03_system_commands.ps1` |
| `verify_app_t04_t05_data_params.ps1` | T04/T05 业务参数持久化与数据面 | 1) 参数块版本为 `0x00020000`，含变比、三路阈值、上报间隔、告警模式与告警环元数据；2) 阈值默认 `21.59f`，变比/阈值写前做合法性校验，拒绝 NaN/Inf；3) `0x02`/`0x04` 命令族完整分发；4) 浮点走 `CimcProtocol_WriteFloatBe` / `Rs485_Service_ReadFloatBe` 大端；5) CH0 取 `adc_result.ch0.smooth`、CH1 取 `adc_result.ch1.smooth`、CH2 取外部 ADC 结果；6) 同样禁止出现 `bl_commit_param(` | `powershell -File tools/verify_app_t04_t05_data_params.ps1` |
| `verify_app_t06_dac_linkage.ps1` | T06 DAC 联动 | 1) DAC 使用直流直写模式，禁止依赖软件触发；2) 12 位上限宏为 `4095`；3) `Dac_Service_Init` 用持久化码值恢复输出；4) 参数块含 `dac_raw` 字段；5) `0x0301` 只接受 2 字节大端、拒绝超范围值、先持久化再回 OK；6) `0x03` 命令族被路由 | `powershell -File tools/verify_app_t06_dac_linkage.ps1` |
| `verify_app_t07_auto_report.ps1` | T07 自动上报状态机 | 1) 上报命令 `0x0302`、载荷 12 字节、时间戳 + CH0 + CH1 大端浮点；2) 支持 1s/3s/5s 三档间隔；3) `0x0302` 启动、`0x0303` 停止且停止回 OK；4) 上报期间只响应 `0x0303`，解析错误静默；5) 采集 LED 状态由上报服务控制；6) 上报服务禁止引用 CH2/PT100 逻辑 | `powershell -File tools/verify_app_t07_auto_report.ps1` |
| `verify_app_t08_alerts.ps1` | T08 告警记录 | 1) 保留 10 条记录；2) 告警区位于参数页内 `+0x0600` 的保留段；3) 提交时先缓存整页再整页回写；4) 只监测 CH0/CH1，用「超阈值上升沿」触发；5) 主动模式判断 `0x01`，主动告警与历史查询都走原始文本；6) 空历史回 `empty`；7) 清除与初始化都重取基线；8) 短窗口原始文本回显抑制存在 | `powershell -File tools/verify_app_t08_alerts.ps1` |
| `verify_app_t09_sleep_wakeup.ps1` | T09 睡眠唤醒 | 1) RTC ALARM0 配 10s、只匹配秒；2) 进 `pmu_to_deepsleepmode` 而不是 standby 复位；3) 唤醒后恢复 `SystemInit`/`SystemCoreClockUpdate`/`systick_config`；4) 睡前关 OLED、唤醒后重开；5) 唤醒直发原始 ASCII；6) 中断文件实现 `RTC_Alarm_IRQHandler` 并委托服务层；7) RS485 DMA 发送前清 TC、发送后等 TC | `powershell -File tools/verify_app_t09_sleep_wakeup.ps1` |
| `verify_app_t10_oled_led.ps1` | T10 OLED 与 LED 状态 | 1) 队号行、16px 字号、两行页地址；2) `IDLE` / `AutoSample` 两态文案与填充；3) 旧电压/电阻调试页与 PT100 依赖必须不出现；4) LED2 作为系统心跳、1s 周期、在任务里翻转；5) LED1 保留给自动上报采集指示；6) 调度表保留 LED 任务；7) 文档侧回归（见第 3.4 节，当前因前置文档缺失而失败） | `powershell -File tools/verify_app_t10_oled_led.ps1` |
| `verify_app_three_channel_mapping.ps1` | 三通道采样映射 | 1) `ADC.h` 定义 CH0/CH1 缓冲索引并暴露 `ADC_ReadChannelRaw`；2) `ADC.c` 开启 PC0/PC1 模拟输入，按单次常规通道读取，rank0 映射 CH0 → `ADC_CHANNEL_10`；3) `Adc_service` 分别主动读取 `ADC_CHANNEL_10` / `ADC_CHANNEL_11`，结果结构含 `ch0` / `ch1`；4) CH2 仍由 `Gd30ad3344_service` 提供 | `powershell -File tools/verify_app_three_channel_mapping.ps1` |

### 2.3 BootLoader（5 个）

| 脚本 | 所属阶段 / 主题 | 断言要点 | 运行方式 |
| --- | --- | --- | --- |
| `verify_boot_migration.ps1` | 分层迁移：BootLoader 核心从旧 `BOOTLOADER/` 目录迁入 `Function/`，协议层独立 | 1) `BL_APP1_START_ADDR = 0x08011000`；2) 参数头保留备份副本；3) `bootloader_run()` 位于 `Function/bl_core.c` 且被 `Function.c` 调用；4) 两端协议层都有 CRC16-Modbus 与 CRC32；5) App 侧 OTA 包头校验位于 `Protocol/ota_protocol.c`、OTA 服务改用流式环形缓冲；6) OTA 包上限 `0x00020000`；7) 两个 Keil 工程都收录新文件，且不再引用旧 `BOOTLOADER\(Src\|Inc)` | `powershell -File tools/verify_boot_migration.ps1` |
| `verify_t11_enter_bootloader.ps1` | T11 `0x0501` 进入 BootLoader | 1) 两端 `bl_param.h` 都定义 `FAST_UPGRADE = 0x55AA55AA`；2) App 侧写升级意图、回 OK、`1500ms` 后软复位，并抑制启动心跳；3) BootLoader 消费该意图并打印官方升级提示与 `10s/7s/4s/1s` 倒计时；4) 普通上电仍保留静默 5s 跳 APP；5) BootLoader OLED 用 8×16 字体显示队号与 `Bootloader`；6) BootLoader 逐字节发送有 DE 建立延时、清 TC、等 TBE；7) 文档侧回归（计划文档缺失时自动跳过） | `powershell -File tools/verify_t11_enter_bootloader.ps1` |
| `verify_t12_bootloader_receive_bin.ps1` | T12 官方 bin 接收与暂存 | 1) `DOWNLOADED = 0xA5A55A5A` 两端一致；2) USART 接收缓冲 512 字节、长度用 `uint16_t`、ISR 有边界检查；3) 4 字节魔术字 `5A A5 C3 3C`、16 KiB RAM 暂存、接收期不擦写 Flash；4) 粘连帧处理（固定 26 字节 ASCII 命令前缀、暂存裸字节、消费暂存字节）；5) 流空闲后按页对齐编程下载区并补 `0xFF`；6) 成功持久化 `DOWNLOADED` + 大小 + CRC 并回 OK；7) 失败回 ERROR 但**不得**复位、不得永久停机；8) 文档侧回归（计划文档缺失时自动跳过） | `powershell -File tools/verify_t12_bootloader_receive_bin.ps1` |
| `verify_t13_bootloader_install.ps1` | T13 安装与回滚 | 1) 备份区/下载区地址常量与 `DOWNLOADED`/`FAILED` 状态值；2) `0x0503` 解析器与有界等待窗口；3) OK 先于破坏性搬运，失败路径回 ERROR；4) 安装前验下载区 CRC、安装后验 App1 CRC；5) 备份与回滚都按页对齐窗口长度，禁止整槽备份；6) 成功写 `IDLE` + `UPDATE_OK`，失败写 `FAILED` + `UPDATE_FAIL`；7) ACK 后关中断执行安装与提交；8) 跳转前停 SysTick、清 NVIC、设 `VTOR`、载入 MSP；9) 文档与开发记忆侧回归（开发记忆缺失时自动跳过） | `powershell -File tools/verify_t13_bootloader_install.ps1` |
| `verify_t14_official_sequence.ps1` | T14 连续评测顺序下的身份与波特率 | 1) BootLoader 从 `reserved[32]` 读 App 参数块并校验魔术字、版本、CRC；2) 两端参数块都含 `dac_raw` 以对齐布局；3) 运行期设备 ID 与波特率从持久化参数派生，非法参数回退默认；4) 官方 OTA 波特率固定 `115200`，USART 初始化必须用运行期取值、禁止硬编码回 `19200`；5) `0x0502/0x0503` 接受当前持久化 ID 或广播，OK/ERROR 帧不得硬编码 `0001`；6) 文档与开发记忆侧回归（开发记忆缺失时自动跳过） | `powershell -File tools/verify_t14_official_sequence.ps1` |

### 2.4 N 系列回归（3 个）

| 脚本 | 所属阶段 / 主题 | 断言要点 | 运行方式 |
| --- | --- | --- | --- |
| `verify_n_series_bootloader_stream_rx.ps1` | 字节流接收：把 BootLoader 接收从「单包 IDLE」改成「8192 字节环形流」 | 1) 接收环 8192 字节、头尾指针 `volatile`；2) ISR 每字节只读一次数据寄存器并压环；3) 暴露 `bl_usart_read_byte()` 与 `bl_usart_clear_rx_stream()`；4) 命令解析与裸 bin 接收都按字节流推进；5) 进入升级窗口前清流；6) 等待窗口内重复打印初始化字样 | `powershell -File tools/verify_n_series_bootloader_stream_rx.ps1` |
| `verify_n02_bad_firmware_error_timing.ps1` | 坏固件错误应答时序 | 1) 静默窗口常量为 `100ms`；2) 存在按静默窗口判断的排空函数；3) 魔数校验失败的两条路径（粘连字节、逐字节）都先排空再返回失败 | `powershell -File tools/verify_n02_bad_firmware_error_timing.ps1` |
| `verify_n03_bootloader_app_handoff.ps1` | BootLoader → App 交接 | 1) BootLoader 侧 USART 映射仍为 `USART1` / `USART1_IRQn` / DE 在 `GPIOE` 的 `PIN_8`；2) 交接清理集中在单一 helper 内；3) 关 RBNE/IDLE 中断、禁并清 USART1 NVIC 挂起、拉低 DE、关 USART1 收发与使能；4) 该 helper 必须紧接在 `__disable_irq()` 之前调用 | `powershell -File tools/verify_n03_bootloader_app_handoff.ps1` |

### 2.5 脚本的实际执行结果（撰写时实测）

在 Windows + Windows PowerShell 上逐个执行 17 个脚本，结果全部为退出码 0：

| 结果 | 脚本 |
| --- | --- |
| 通过（17 个） | 第 2.2、2.3、2.4 节列出的全部脚本 |

其中 5 个脚本（T10、T11、T12、T13、T14）除源码断言外还会读取文档作为断言输入。这层依赖已经处理成「文档缺失时跳过而不是失败」：

- T10/T11/T12/T13 查找计划文档时改为在 `docs/` 下递归匹配，并把结果作为可选项参与断言（例如 `tools/verify_app_t10_oled_led.ps1:35-L38`、`:71-L72`）；计划文档属于内部开发记录，公开仓库中不存在时不再让脚本失败。
- T13/T14 查找开发记忆文件时按「工程根目录 → `docs/dev-notes/`」的顺序取第一个存在的路径（例如 `tools/verify_t14_official_sequence.ps1:38-L40`）。
- 人工核验清单的断言仍然要求文件存在，但相关文件的路径与编码已修正（见第 3.1 节说明的编码问题）。

历史情况（供对照）：在这批修正之前，上述 5 个脚本会因为找不到被重命名或移动的文档而以 `Test-Path` 的 `LiteralPath 为 null` 或 `Missing required file` 中止。这属于验证工具与文档组织之间的耦合，与固件正确性无关；如果后续再次调整 `docs/` 结构，需要同步检查这几个脚本的路径解析。

另注：这些脚本用 `[regex]::IsMatch` / `Select-String` 做**文本**断言，不解析 C 语法，因此断言命中不等于语义正确（例如 T11 断言 `while(wait_result == BL_UPGRADE_WAIT_RECEIVED_ERROR)` 存在，而 `CIMC_BootLoader/Function/bl_core.c:1297` 的实际循环形态与该行文字并不完全一致，脚本仍通过）。

## 3. 人工核验清单的用法与覆盖项

两份清单都以 Markdown 复选框（`- [ ]` / `- [x]`）加表头记录栏的形式组织，一份对应协议核心能力，一份对应 RS485 业务链路。它们不是自动化测试，而是把「哪条命令、期望什么、实际看到什么」写死下来，便于逐条复跑与留痕。

### 3.1 `docs/verification/RS485人工核验清单.md`（约 370 行）

**固定记录栏**：测试环境（板卡、固件构建版本、RS485 转接器、默认串口参数、测试日期、测试人）、复测前置检查（是否重新构建并烧录、串口助手是否使用 ASCII 字符串发送模式、参数是否为 `19200 / 8 / 1 / None`）、以及每个主题内的「实测记录」小节和文末通用「记录」栏（结果摘要、失败帧、实际应答）。

**覆盖项按主题分组**，每条都是「下发帧（十六进制字符串）+ 期望应答帧（带解析含义）+ 观测点」三件套：

| 主题分组 | 覆盖内容 |
| --- | --- |
| A 系列：广播寻址 | 旧 demo 文本不再出现；上电/复位心跳；广播帧与心跳结构 `A5B6 <ID> 05 8888 00 02 <CRC> B6A5`；心跳 ID 与当前配置一致 |
| B/C/L/M 系列：系统管理 | 重启先回 OK 再复位；固件版本查询；设备 ID 查询/设置与旧 ID 静默；波特率查询/设置与编码表（`0x11=4800`…`0x14=115200`）；重启后 ID 与波特率保持 |
| B/D/E/F 系列：数据、变比、阈值 | CH0/CH1 查询；变比设置后数值按变比变化；上报间隔设置；CH0/CH1/CH2 阈值批量读、单路读、单路写；写入后重启再读的保持性（已勾选通过） |
| G 系列：DAC 联动 | CH1 变比复位为 1.0；DAC=0 / 2048 / 4095 三次设置与回读；DAC=4096 必须回通用错误帧 |
| K 系列：异常帧 | CRC 错、长度不匹配、未知帧类型、未知命令字都要回错误应答；本机发出被回灌的响应帧/错误帧不得再次触发应答；非广播且 ID 不匹配的帧不回任何字节 |
| 记录节 | T04/T05、T06 首次与复测、ADC 动态采样修正、T07 上报、T08 告警、T09 睡眠唤醒、T12 接收与修正，均以「时间 + 下发帧 + 实际应答 + 解析值 + 结论」记录 |
| T07 | 12 字节上报帧结构与约 1s 周期；上报期间版本查询无响应无错误帧；OLED 显示 `AutoSample`、采集 LED 常亮；停止后停发并熄灭；3s/5s 档位留作抽查 |
| T08 | 清除后查询回 `empty`；主动模式下越阈直发一行文本并写入历史；仅存储模式下越阈只存储；历史最新在前；再次清除回 `empty` |
| T09 | 睡眠先回 OK；约 10s 后唤醒；唤醒直发原始 ASCII 而不是协议帧；唤醒后版本查询恢复；低功耗电流项至今未勾选 |
| T10 | OLED 两行内容与字号、`AutoSample` 与 `IDLE` 切换、LED1/LED2 行为、睡眠期间 OLED 熄灭、唤醒后恢复 |
| T11 / T12 / T13 | 逐条给出 `0x0501`、`0x0502`、`0x0503` 的下发帧、期望 OK/ERROR 帧、倒计时文本、OLED 文案、上位机切片参数与失败路径复测要求 |
| T16 | CH2 的查询、阈值读写、自动上报与告警统一压到这里，依赖外部 PT100 / 外部 ADC 硬件 |

编码说明：两份清单历史上保存为 GBK 与 UTF-8 混合，其中的中文局部字节序列不是合法 UTF-8，导致按 UTF-8 读取的工具（编辑器、脚本、差异比较）在该处报错或显示乱码。现已统一转存为 UTF-8（无 BOM）。后续手工编辑时请保持 UTF-8 保存，否则同样的读取问题会重新出现。

### 3.2 `docs/verification/协议人工核验清单.md`（约 2.8 KB）

用途是把赛题给出的示例帧逐条落成可复跑的输入输出对，并单独列出解析器的拒绝行为。

- **官方示例帧**（21 条）：重启、固件版本查询、设备 ID 查询、波特率查询、广播寻址、心跳应答、通用错误应答、CH0 查询与应答示例、CH2 查询与应答示例、设置 CH0 变比、设置上报间隔、自动上报结构、批量/单路阈值读取、写 CH0 阈值。以「下发帧」与「期望帧/期望结构」成对列出。
- **解析器人工用例**（10 条）：小写十六进制是否被接受或先被规范化；帧前后空白由服务层剥离；非十六进制字符、奇数个字符、短于最小固定长度、帧头错、帧尾错、长度字段与载荷不一致、CRC 不匹配、协议版本不是 `0x02` 这九类输入都必须被拒绝。
- **记录栏**：测试人、测试日期、板卡、结果摘要。

### 3.3 两份清单怎么用

1. 先在 Keil 中重新构建受影响工程并烧录（清单里每个主题都单独列了「已重新构建 / 已烧录」前置项）。
2. 串口助手必须用 ASCII 字符串发送模式（不是 HEX 发送），因为协议以文本形式收帧。
3. 逐条发送「下发帧」，把实际收到的字节原样抄进「实际应答」，与「期望」比对；不一致时先确认固件版本，再记录失败帧。
4. 涉及物理量的条目（OLED、LED、电流、PA4–PC1 跳线、PT100 档位）必须现场观察，不能只看串口。
5. 新增结论写进对应主题的「实测记录」小节，保留原始帧文本。

## 4. 人工核验能证明什么、不能证明什么

清单里的勾选项是唯一带有「实机做过」性质的材料，但它的证据强度有明确边界，引用时不应越界。

**能证明的**

- 某条具体帧在同一台板卡上得到过预期应答，且应答的十六进制字节被完整抄录下来，事后可比对。
- 某条命令不会产生额外输出（例如上报期间查询版本无响应、非寻址帧不回字节）。
- 若干物理观测点成立：屏幕文案、指示灯状态、跳线是否接上、按时间读秒得到的间隔量级。

**不能证明的**

- 统计意义上的稳定性。清单里每个条目通常只跑一两次，「通过」不代表可重复。清单中出现的间隔值（例如上报间隔约 1.08s）是人工读秒的观察值，不是仪器测量值。
- 边界与时序的严格性。倒计时文本的毫秒节奏、方向控制建立延时的最小可用值、关闭中断窗口的实际时长都没有被测量。
- 与其他板卡、其他上位机、其他波特率的兼容性。清单记录的环境栏虽留有位置，但历史记录基本集中在单一硬件组合上。
- 覆盖完整性。清单条目来自对赛题交互的理解，可能存在未列出的交互路径；「清单全勾」不等于「应当验证的都验证过了」。

**引用建议** 在对外说明中，把清单结论写成「在 X 环境下观测到 Y」而不是「Y 成立」，把静态脚本结论写成「实现形态未回退」而不是「功能正确」。

## 5. 典型验证场景

以下路径都是实际跑过的组合，按「步骤 → 观察手段」列出。前四步属常规回归，升级链路属破坏性验证。

**场景 A：上电 → 心跳 → 系统管理命令**

1. 烧录 App，复位板卡。
2. 观察心跳：串口助手在 15s 内应收到 `A5B6 0001 05 8888 00 02 <CRC> B6A5`。心跳由 `Rs485_Service_Init()` 登记、在 RS485 任务里延迟 500ms 发出（`CIMC_App/Function/Rs485_service.c:542-L561`、`:1134-L1138`），观察手段是串口日志 + 时钟。
3. 发送重启帧 `A5B60001010101000215ABB6A5`：先收到 OK `A5B600010201010102FF0C45B6A5`，随后复位。确认手段是串口顺序 + LED2 心跳中断后重新开始。
4. 发送波特率查询/设置帧：观察旧波特率收到 OK、切换后新波特率可通；随后断电重上电，再查询确认 ID 与波特率仍为新值。

**场景 B：参数掉电保存**

1. 写入 CH0 变比、CH1 变比、三路阈值、上报间隔、告警模式与 DAC 码值。
2. 记录写入后即时查询的返回值。
3. 断电、重新上电，逐个查询并比对。观察手段是串口返回值对照 + 记录在核验清单的「重启后」条目。
4. 已确认的项：重启后批量阈值返回 `21.59 / 21.59`；重启后 CH0 返回约 `71247`，与设置变比前约 `3300 × 21.59` 一致。

**场景 C：三通道数据面与 DAC 联动**

1. 转动 PC0 板载电位器，查询 `0x0201`，数值应随之变化。
2. 确认 PA4 已跳线到 PC1。
3. 依次发送 DAC=0 / 2048 / 4095，每次先看 `0x0301` 是否回 OK，再查 `0x0202`。
4. 复测记录：DAC=0 → CH1 `3`；DAC=2048 → CH1 `1649`；DAC=4095 → CH1 `3290`（见核验清单「T06 复测记录」）。观察手段是串口返回值转 float。
5. 发送 DAC=4096，必须收到通用错误帧 `A5B60001FFEEEE0002E087B6A5`。

**场景 D：自动上报与告警**

1. 设置 1s 间隔，发 `0x0302` 启动：观察立即返回的 12 字节上报帧，以及后续约每秒一帧的节奏（实测约 1.08s）。
2. 上报期间发送版本查询帧：必须无版本响应、无错误帧，只有周期上报帧。
3. 同时观察 OLED 第二行变为 `AutoSample`、LED1 常亮、LED2 继续 1s 闪烁——这三项是物理观察，串口日志不能替代。
4. 发 `0x0303` 停止：回 OK，停止上报，LED1 熄灭，OLED 回到 `IDLE`。
5. 告警：先把阈值调到高于当前值，再把阈值压到低于当前值（例如 CH0 阈值 4000 → 1），主动模式下应直发一行 `YYYY-MM-DD HH:MM:SS | CH0 | 1.00 | <actual>`，随后 `0x0602` 查询可读回同一行；仅存储模式下不直发但可查询；`0x0603` 清除后查询回原始文本 `empty`。

**场景 E：睡眠与唤醒**

1. 发送 `A5B600010103AA00024DDBB6A5`，先收到 OK `A5B600010203AA0102FFE81CB6A5`。
2. 观察 OLED 熄灭（`Sleep_service.c:107` 在 WFI 之前关闭显示）。
3. 约 10s 后收到原始字符串 `instrument wakeup`；实测唤醒耗时约 10.1s。
4. 唤醒后发送版本查询，确认主循环与外设已恢复。
5. 低功耗电流至今只列为待抽查项，没有电流表读数据。

**场景 F：时序与分帧相关的回归（本项目历史上最容易出问题的一类）**

以下四条都曾真实出现过现象，复现方式与观察手段固定：

1. RS485 发送末尾被截断：接收方收到的字符串比预期少 1~2 个字节（曾出现 `instrument wake` 少尾部 `up`）。观察手段是把同一条原始文本重复发送多次并统计长度。修正点在发送路径的启动时序与释放时序（`CIMC_App/Driver/RS485/rs485_dma.c:212-L232`）。
2. 本机回显导致错误帧连发：串口助手看到同一条错误帧连续刷屏。原因是 RS485 半双工总线把本机发出的响应帧回灌到接收端，又被当成异常输入。修正点是分发前的帧类型过滤（`CIMC_App/Function/Rs485_service.c:1053-L1062`）与原始文本短窗口抑制（`Rs485_service.c:30-L31`、`:1173-L1178`）。观察手段是观察连续发送同一条非法帧时的应答数量。
3. 官方 bin 与命令粘连：上位机把 26 字节的 `0x0502` 命令与随后的裸 bin 字节放进同一个空闲帧。观察手段是抓取 BootLoader 侧的接收日志，确认首个字节不是 `0x5A`。修正点是只解码固定的 ASCII 命令前缀并把裸字节后缀暂存（`CIMC_BootLoader/Function/bl_core.c:463-L500`、`:569`、`:653-L658`）。
4. 坏固件的错误应答被流动的原始数据淹没：发送错误 bin 后接收方在等待窗口内只看到 App 心跳。观察手段是复现时把错误镜像的发送时长与 BootLoader 的应答时刻对齐。修正点是先按静默窗口排空再回 ERROR（`bl_core.c:737-L757`）。

**场景 G：完整官方升级链路（破坏性）**

1. APP 运行中发送 `0x0501`（`A5B60001010501000225AAB6A5`）：先收到 OK `A5B600010205010102FFCCB4B6A5`；APP 写升级意图、抑制启动心跳、延迟 1500ms 后软复位（`Rs485_service.c:528-L540`、`CIMC_App/Function/BootUpdate_service.c:235`）。
2. BootLoader 运行：物理观察 OLED 显示队号 + `Bootloader`，串口收到 `using command to interrupt start Application` 与 `10s/7s/4s/1s` 倒计时文本（`CIMC_BootLoader/Function/bl_core.c:838-L860`）。
3. 10s 窗口内发送 `0x0502`（`A5B600010105020002255AB6A5`），随后用官方上位机传输 `V1版本固件.bin`（6408 字节，256 字节切片）。BootLoader 按字节流接收、剥离 4 字节魔术字、暂存 RAM 并累计 CRC32（`bl_core.c:758-L830`）。
4. 传输完成收到 `0x0502` OK `A5B600010205020102FF88B4B6A5`；此时参数页落的是 `DOWNLOADED` + payload 大小 + payload CRC（`bl_core.c:1289-L1294`）。
5. 发送 `0x0503`（`A5B600010105030002E50BB6A5`）：先收到 OK `A5B600010205030102FF74B5B6A5`，随后进入关中断的 Flash 安装窗口，串口静默属预期（`bl_core.c:1338-L1340`）。
6. 安装流程按页对齐窗口备份 App1 → 搬运 → 复核 CRC → 提交参数与日志（`bl_core.c:1053-L1159`）。
7. 复位后把上位机切到 115200，等待新固件心跳，确认安装生效。
8. 失败路径：用首 4 字节不是 `5A A5 C3 3C` 的文件，应收到 `0x0502` ERROR `A5B60001FF05020002F173B6A5`，且 BootLoader 保持升级会话等待重试，不擅自复位回 APP（`bl_core.c:1297-L1310`）。

## 6. 未覆盖与后续计划

以下条目在撰写时确认没有证据支撑，按主题列出，供后续补齐：

**升级与掉电**

- 破坏性窗口掉电覆盖：参数页擦除、下载区擦除、App1 搬运、备份写入、参数与日志提交五个点都没有逐点切电记录。`docs/ota.md` 第 11 节把该项列为「未验收」。计划做法是按窗口逐点切电并在每次上电后读回 `update_flag`、`app_size`、`app_crc32` 与日志条目。
- 参数主备同页：主备副本在同一 4 KiB 页内（`CIMC_App/common/bl_param.h:11-L12`），整页擦除窗口内掉电可能两份同时失效，此时只能回默认参数。未做量化验证。
- 写入未回读：App 侧 `InternalFlash_WriteBytes()` 不读回比较，`AppParam_Service_Commit()` 不检查写入结果，因此 RS485 的 OK 应答不能等同于持久化成功（`CIMC_App/Function/App_param_service.c:222-L225`）。待补：写入后读回校验与失败上报。
- App 流式 OTA 未闭环：BootLoader 只处理 `FAST_UPGRADE` 与 `DOWNLOADED`，没有 `PENDING` 分支（`CIMC_BootLoader/Function/bl_core.c:1280`、`:1318`）。该路径当前无法端到端安装。
- 无签名、无安全启动、无防回退：完整性只靠 CRC32，BootLoader 不读取也不比较 `OTA_PACKAGE_APP_VERSION`，无法阻止刷入更旧或非授权镜像。
- 备份元数据不足：备份区不保存旧镜像的完整长度与独立 CRC，备份长度按新镜像的 `erase_size` 决定（`bl_core.c:966-L977`）。
- 向量表校验偏宽：`bl_is_app_vector_valid()` 只要求 MSP 落在 SRAM、Reset Handler 落在整片 Flash 范围，不检查 Thumb 位、不限定落在 App1 区间（`bl_core.c:242-L263`）；RAM 暂存检查同样宽（`:663-L684`）。
- 流式路径容量边界：包头上限为 `0x00020000`，App 侧环形缓冲为 32 KiB（`CIMC_App/Function/BootUpdate_service.c:13`），缓冲追赶能力与实际可用速率无量测记录。

**驱动与协议**

- RS485 DMA 发送等待 `FTF` / `TC` 的两个 `while` 都没有超时。一旦外设异常会死等（`CIMC_App/Driver/RS485/rs485_dma.c:221-L229`）。待补：超时退出与错误上报。
- RS485 接收只有单帧缓冲，没有多帧队列；超长无间隙输入与总线持续繁忙场景未做压力测试。
- 非 DMA 模式（`RS485_MODE_IDLE`）的发送路径只等 `TBE` 与 `TC`，没有清陈旧 `TC` 标志（`CIMC_App/Driver/RS485/rs485_idle.c:112-L129`）。当前默认为 DMA 模式（`CIMC_App/Driver/RS485/RS485.h:37`），切模式后需重新验证截断问题。
- 外部 ADC 采样有效性未在协议出口校验：读取失败会清 ready 标志，但应答路径不检查 ready，异常时可能把旧值当新值返回。

**系统与硬件**

- 长时间稳定性：没有连续运行记录（小时级/天级），内存与栈水位、任务抖动、RTC 长漂移均无量测。
- 总线多节点：只在单机 + RS485 转接器的拓扑下验证过，未做多节点并发寻址与冲突场景。
- EMC / 浪涌：无测试方案与报告。
- 采样精度标定：CH0/CH1 的换算链路只验证了「随激励单调变化」，没有与标准源比对精度；CH2 链路止于电阻换算，PT100 电阻到温度的换算未实现，对应的查询、阈值、上报、告警统一压在 T16 且依赖外部硬件。
- 低功耗电流：`0x03AA` 的串口行为已复测通过，但电流表读数只列为待抽查项。
- 硬件侧交付物：电源板带载与温升实测报告、纹波与效率测量、原理图 PDF 导出、BOM 与实物照片尚未整理。

**验证基础设施**

- 无自动化测试框架、无 CI、无覆盖率统计。
- 部分脚本把文档文件当作断言输入（详见第 2.5 节），文档改名或移动可能导致脚本失败或跳过断言。
- 脚本断言以文本正则为主，不校验宏的定义、函数返回值语义与 C 语法结构。

## 7. 如何复现

### 7.1 环境要求

- Windows 10/11。
- PowerShell。脚本本身只用到 `$PSScriptRoot`、`Join-Path`、`Get-Content`、`Select-String`、`[regex]::IsMatch`，在 Windows PowerShell 5.1 与 PowerShell 7 上都能跑。本文示例统一使用 Windows 自带的 `powershell`；装有 PowerShell 7 时把 `powershell` 换成 `pwsh` 即可，实测两者行为一致。第 2.5 节的结果表是在 Windows PowerShell 5.1 上实测的。
- 需要实机验证或重构建时：Keil MDK 5（ARMCC）+ GigaDevice GD32F4xx 器件支持包，或 VSCode + EIDE。
- 需要 OTA 全链路复现时：官方上位机、RS485 转接器、`115200 / 8 / 1 / None` 的串口参数，以及官方 `V1版本固件.bin` / `V2版本固件.bin`。

静态脚本不需要 Keil，也不需要板卡。

### 7.2 运行单个脚本

```powershell
powershell -File tools/verify_t12_bootloader_receive_bin.ps1
powershell -File tools/verify_n03_bootloader_app_handoff.ps1
powershell -File tools/verify_app_t09_sleep_wakeup.ps1
```

带参数的脚本可以显式指定工程根目录（不传参时使用脚本上一级目录）：

```powershell
powershell -File tools/verify_app_t04_t05_data_params.ps1 -ProjectRoot C:\path\to\CIMC-Industrial-Embedded-2026
powershell -File tools/verify_boot_migration.ps1 -Root C:\path\to\CIMC-Industrial-Embedded-2026
```

### 7.3 一次性跑完全部脚本

```powershell
$fail = @()
foreach ($s in Get-ChildItem tools -Filter *.ps1 | Sort-Object Name) {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $s.FullName
    if ($LASTEXITCODE -ne 0) { $fail += $s.Name }
}
$fail
```

成功时每个脚本打印一行 `... passed.`；失败时打印原因行。按第 2.5 节的实测记录，当前 17 个脚本全部通过；脚本数少于 16 或出现「Missing pattern」时，先确认工程目录结构未被改动，再确认断言对应的源码位置是否有意修改。

### 7.4 复现实机行为

静态脚本之外的行为需要按第 3 节的两份清单逐条复跑，环境准备与记录方式见清单的「测试环境」「复测前置检查」两节。涉及 Flash 擦写的 `0x0503` 步骤会破坏 App1，复现前先确认手上有可用的 App 构建产物，并知道如何重新烧录。

### 7.5 复现顺序建议

1. 跑静态脚本，确认没有回归。
2. 按 `docs/verification/协议人工核验清单.md` 逐条复跑协议示例帧与解析器拒绝用例。
3. 按 `docs/verification/RS485人工核验清单.md` 的 A/B/C/L/M → B/D/E/F → G → K → T07 → T08 → T09 → T10 顺序复跑业务链路，并在场景 F 的四条时序回归上各复跑一次。
4. 最后单独安排 T11 → T12 → T13 的升级链路（场景 G），并预留重烧 App 的时间。

## 8. 依据

- 脚本：`tools/` 下 17 个 `.ps1`，逐份阅读并实际执行。
- 人工核验清单：`docs/verification/RS485人工核验清单.md`、`docs/verification/协议人工核验清单.md`。
- 源码：`CIMC_App/`、`CIMC_BootLoader/` 下与本文件相关的 Driver / Function / Protocol / common 文件，行号已在正文标注。
- 交叉参考：`docs/ota.md`（分区、升级状态机与已知边界）、`docs/dev-notes/PROJECT_MEMORY.md`（开发过程中的现象与结论来源）。开发记忆与源码冲突时以源码为准。
