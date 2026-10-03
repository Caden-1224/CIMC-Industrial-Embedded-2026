# RS485 通信协议规范

本文描述 GD32F470 工业多通道数据采集与远程升级终端（`CIMC_App/` 与 `CIMC_BootLoader/` 两套 Keil 工程）在上位机侧可见的通信协议：USART1 + RS485 的 ASCII-Hex 命令/应答协议，以及与之相邻的 USART0 流式 OTA 包头格式。

行号引用的格式为 `相对工程根目录的路径:行号`，指向本次撰写时读取的源码版本。文中所有「已实现」结论均来自源码；无法从源码确认的内容标注为「待核实」；仅来自设计意图、开发记忆或未闭环的部分标注为「未实现」或「设计推演」。本文不做构建、烧录或实机验收声明，USB/总线电气特性亦不在范围内。

与本文相邻的文档：Flash 分区、OTA 状态机与安装/回滚实现见 `docs/ota.md`；引脚分配见 `docs/hardware-pinout.md`。

## 1. 总览

### 1.1 分层

| 层 | 内容 | 依据 |
| --- | --- | --- |
| 物理层 | USART1 异步串口 + RS485 半双工差分收发，DE/RE 由 GPIO 控制收发方向 | `CIMC_App/Driver/RS485/RS485.h:41-L46` |
| 传输层 | ASCII-Hex：每个协议字节由两个可打印十六进制字符承载，大写发送、大小写均可接收 | `CIMC_App/Protocol/cimc_protocol.c:4-L29`、`:112-L141` |
| 应用层 | 定长 13 字节头 + 变长内容的二进制帧，CRC16-Modbus 校验 | `CIMC_App/Protocol/cimc_protocol.c:143-L255` |
| 文本旁路 | 告警主动上报、告警历史查询、睡眠唤醒串、BootLoader 升级提示直发原始 ASCII，不做帧封装 | `CIMC_App/Function/Rs485_service.c:204-L231`、`CIMC_BootLoader/Function/bl_core.c:838-L860` |

上位机通过 USART1（RS485）下发命令帧；设备以同一通道回帧或直发文本。另有 USART0（PA9/PA10，115200 8N1）承载带头二进制流的 App 侧 OTA 通道，其包格式与 RS485 命令帧不同（见第 7 节）。

### 1.2 物理层参数

| 项目 | App 侧 | BootLoader 侧 | 依据 |
| --- | --- | --- | --- |
| 外设 | USART1 | USART1 | `CIMC_App/Driver/RS485/RS485.h:41`、`CIMC_BootLoader/Driver/USART/usart.h:18` |
| TX / RX | PD5 / PD6（AF7） | PD5 / PD6（AF7） | `CIMC_App/Driver/RS485/RS485.h:42-L44`、`CIMC_BootLoader/Driver/USART/usart.h:17-L20` |
| DE/RE | PE8（板载 485_CS），高=发送，低=接收 | PE8 | `CIMC_App/Driver/RS485/RS485.h:45-L46`、`CIMC_BootLoader/Driver/USART/usart.h:25-L27` |
| 帧格式 | 8 数据位 / 无校验 / 1 停止位（8N1） | 由 `usart_deinit()` 后仅设置波特率，默认 8N1 | `CIMC_App/Driver/RS485/rs485_dma.c:130-L136`、`CIMC_BootLoader/Driver/USART/usart.c:78-L82` |
| 波特率 | 由持久化的波特率编码决定，默认 `0x13` = 19200 | 官方升级通道固定 115200 | `CIMC_App/Function/App_param_service.c:258-L289`、`CIMC_BootLoader/Function/bl_core.c:40`、`:136` |
| 接收方式 | 默认 DMA 收 + IDLE 分帧（可编译期切换为中断收） | RBNE 中断逐字节入 8192 字节环形缓冲 | `CIMC_App/Driver/RS485/RS485.h:36-L38`、`CIMC_BootLoader/Driver/USART/usart.c:17`、`:99-L117` |
| 发送方式 | DE 拉高 → DMA 搬运 → 等 FTF → 等 USART TC → DE 拉低 | 逐字节：DE 拉高 → 清旧 TC → 等 TBE → 写数据 → 等 TC → DE 拉低 | `CIMC_App/Driver/RS485/rs485_dma.c:202-L236`、`CIMC_BootLoader/Driver/USART/usart.c:176-L190` |

收发方向切换的关键点：DMA 传输完成标志 FTF 只表示数据已搬到 `USART_DATA`，最后一字节真正离开移位寄存器要等 `USART_FLAG_TC`，因此 DE 必须在 TC 之后才拉低；BootLoader 侧还在每个字节发送前清除上一次遗留的 TC 粘性标志（`CIMC_BootLoader/Driver/USART/usart.c:15`、`:184`）。App 侧同一处理见 `CIMC_App/Driver/RS485/rs485_dma.c:212-L218`。

### 1.3 接收与分帧

App 侧 DMA 模式下，IDLE（总线空闲一个字节时间）触发中断后关 DMA 通道，用「接收容量 256 − DMA 剩余传输数」算出本帧字节数，复制到单帧缓冲 `s_frame_buffer` 并置 `s_frame_ready`，随后重设 DMA 地址与长度并重启通道（`CIMC_App/Driver/RS485/rs485_dma.c:288-L328`）。服务层取到帧后先裁剪首尾空白（`\r`、`\n`、空格、`\t`），再做 ASCII-Hex 解码与帧校验（`CIMC_App/Function/Rs485_service.c:48-L93`、`:1155-L1198`）。分帧依赖 IDLE 间隙，不保证任意粘包/半包可增量恢复，详见第 9 节。

## 2. 帧格式

### 2.1 逐字节偏移表

固定开销 13 字节，加 `content_len`（0..255）字节内容，总长 `13 + content_len`。字段多字节量一律大端（网络序）传输。

| 偏移 | 长度 | 字段 | 取值与说明 | 依据 |
| --- | --- | --- | --- | --- |
| 0 | 2 | 帧头 | 固定 `0xA5B6`，线上先发 `A5` 后发 `B6` | `CIMC_App/Protocol/cimc_protocol.h:7`、`.c:168-L171` |
| 2 | 2 | 设备 ID | `0x0001`..`0xFFFE` 为单播；`0xFFFF` 为广播 | `CIMC_App/Protocol/cimc_protocol.h:11-L13` |
| 4 | 1 | 帧类型 | `0x01` 命令、`0x02` 应答、`0x05` 心跳、`0xFF` 错误 | `CIMC_App/Protocol/cimc_protocol.h:15-L18` |
| 5 | 2 | 命令字 | 大端；`0x8888` 心跳、`0xFFFF` 广播发现、`0xEEEE` 通用错误 | `CIMC_App/Protocol/cimc_protocol.h:20-L22` |
| 7 | 1 | 内容长度 | `content_len`，仅统计内容字节，不含头部/CRC/帧尾 | `CIMC_App/Protocol/cimc_protocol.c:177-L182` |
| 8 | 1 | 协议版本 | 固定 `0x02`，不匹配即拒绝整帧 | `CIMC_App/Protocol/cimc_protocol.h:9`、`.c:184-L187` |
| 9 | n | 内容 | 命令/应答载荷，n = `content_len` | `CIMC_App/Protocol/cimc_protocol.c:201-L204` |
| 9+n | 2 | CRC16 | CRC16-Modbus 结果，大端写出 | `CIMC_App/Protocol/cimc_protocol.c:249-L250` |
| 11+n | 2 | 帧尾 | 固定 `0xB6A5` | `CIMC_App/Protocol/cimc_protocol.h:8`、`.c:172-L175` |

解析顺序（`CIMC_App/Protocol/cimc_protocol.c:143-L207`）：最小长度 13 → 最大长度 268 → 帧头 → 帧尾 → `content_len` 与实际长度一致 → 协议版本 → CRC。任一步失败返回独立的错误码（`CIMC_App/Protocol/cimc_protocol.h:31-L45`），不进入命令分派。

### 2.2 大端字段说明

| 位置 | 编码 | 示例 |
| --- | --- | --- |
| 帧头 / 帧尾 / 设备 ID / 命令字 / CRC16 | 大端 16 位，高字节先发 | `0xA5B6` → `A5 B6` |
| 32 位整型载荷（Unix 时间戳） | 大端 32 位 | `0x5F5E1000` → `5F 5E 10 00` |
| 单精度浮点载荷 | IEEE754 binary32，大端字节序 | `1.0f` → `3F 80 00 00` |

读写实现分别为 `CimcProtocol_ReadBe16/ReadBe32/WriteBe16/WriteBe32/WriteFloatBe`（`CIMC_App/Protocol/cimc_protocol.c:330-L388`）。

### 2.3 设备 ID 规则

- 合法本机 ID 范围 `0x0001..0xFFFE`；`0xFFFF` 保留为广播地址，不可设置为本机 ID（`CIMC_App/Protocol/cimc_protocol.h:11-L13`、`CIMC_App/Function/App_param_service.c:43-L46`）。
- 接收侧只处理「设备 ID 等于本机 ID」或「等于广播 `0xFFFF`」的帧，其余单播帧静默丢弃（`CIMC_App/Function/Rs485_service.c:162-L171`、`:1048-L1051`）。
- 应答帧的设备 ID 始终填本机当前 ID，广播命令亦按本机 ID 单播应答（`CIMC_App/Function/Rs485_service.c:173-L202`）。多节点同挂一条总线时，广播命令会引发多机同时应答，当前实现没有随机退避或应答延时。
- BootLoader 侧接受「持久化 ID」或广播 `0xFFFF`，ID 从 App 参数块继承，非法时回退 `0x0001`（`CIMC_BootLoader/Function/bl_core.c:98-L101`、`:642-L644`、`:135`）。

### 2.4 帧类型与处理规则

| 收到的帧类型 | 处理 | 依据 |
| --- | --- | --- |
| `0x01` 命令 | 通过 ID 过滤后按命令字分组分派 | `CIMC_App/Function/Rs485_service.c:1089-L1131` |
| `0x02` 应答 / `0xFF` 错误 | 静默丢弃（用于抑制半双工总线上的本机回灌） | `CIMC_App/Function/Rs485_service.c:1058-L1062` |
| `0x05` 心跳且命令字 `0xFFFF` | 视为广播发现，回一帧心跳 | `CIMC_App/Function/Rs485_service.c:1077-L1082` |
| `0x05` 心跳且命令字非 `0xFFFF` | 静默丢弃 | `CIMC_App/Function/Rs485_service.c:1084-L1087` |
| 其他帧类型 | 回通用错误帧（命令字 `0xEEEE`） | `CIMC_App/Function/Rs485_service.c:1089-L1093` |

自动上报运行期间，除停止命令 `0x0303` 外所有帧（含解析失败的帧）一律静默，不回应答也不回错误帧（`CIMC_App/Function/Rs485_service.c:1068-L1075`、`:1179-L1193`）。

设备上电初始化完成后约 500 ms 主动发送一次心跳帧（帧类型 `0x05`、命令字 `0x8888`、内容长度 0），用于在线通知；该启动心跳在收到 `0x0501` 升级请求后被抑制（`CIMC_App/Function/Rs485_service.c:15`、`:542-L561`、`:528-L540`）。

## 3. 校验

### 3.1 CRC16-Modbus 参数

| 项目 | 取值 | 依据 |
| --- | --- | --- |
| 算法 | CRC-16/MODBUS，逐位实现（无查表） | `CIMC_App/Protocol/cimc_protocol.c:31-L59` |
| 初值 | `0xFFFF` | `CIMC_App/Protocol/cimc_protocol.c:33` |
| 反射多项式 | `0xA001`（即多项式 `0x8005` 的反射形式），右移实现 | `CIMC_App/Protocol/cimc_protocol.c:49` |
| 输入 / 输出反射 | 是（逐字节异或后右移 8 次） | `CIMC_App/Protocol/cimc_protocol.c:44-L55` |
| 最终异或 | 无（函数直接返回累加值） | `CIMC_App/Protocol/cimc_protocol.c:58` |

### 3.2 覆盖范围

校验范围是**解码后的二进制帧前 `9 + content_len` 字节**，即「帧头 + 设备 ID + 帧类型 + 命令字 + 内容长度 + 协议版本 + 内容」，不含 CRC 字段本身与帧尾（`CIMC_App/Protocol/cimc_protocol.c:189-L194` 校验、`:249-L250` 生成）。

空内容帧（`content_len = 0`）的校验范围为前 9 字节，完整帧长 13 字节。

### 3.3 线上字节序

CRC 结果通过 `WriteBe16` 以大端写入、通过 `ReadBe16` 以大端读回（`CIMC_App/Protocol/cimc_protocol.c:189`、`:250`、`:330-L338`、`:353-L362`）。因此这里**与标准 Modbus RTU 不同**：标准 Modbus RTU 的 CRC 字段是低字节先发，本协议是高字节先发。按标准 Modbus 的字节序去拼接本协议帧会得到反序的 CRC 字段。

CRC 只覆盖应用层二进制帧，ASCII-Hex 编码后的字符不参与校验（ASCII 层的错误由「非法字符 / 奇数长度」直接拒绝）。

### 3.4 与 OTA 用的 CRC32 的区别

OTA 元数据使用另一套 CRC32：反射多项式 `0xEDB88320`、初值 `0xFFFFFFFF`、最终异或 `0xFFFFFFFF`，等价于 zlib / `binascii.crc32`（`CIMC_App/Protocol/cimc_protocol.c:291-L328`）。两者层级不同：CRC16 保护单帧链路完整性，CRC32 保护固件镜像、参数块与 OTA 包头（`CIMC_App/Protocol/ota_protocol.c:26-L28`、`CIMC_BootLoader/Function/bl_core.c:730-L732`）。CRC 只能检错，不能认证，详见第 9 节。

## 4. ASCII-Hex 编解码规则

| 规则 | 实现 | 依据 |
| --- | --- | --- |
| 编码 | 每字节输出两个大写十六进制字符，使用 `0123456789ABCDEF` | `CIMC_App/Protocol/cimc_protocol.c:24-L29`、`:133-L140` |
| 解码字符集 | `0-9`、`A-F`、`a-f` 均接受 | `CIMC_App/Protocol/cimc_protocol.c:4-L22` |
| 奇数长度 | 在解码前直接拒绝（返回 `CIMC_PARSE_ASCII_ODD_LENGTH`），不进入 CRC 判断 | `CIMC_App/Protocol/cimc_protocol.c:84-L87` |
| 非法字符 | 任一字符不是合法十六进制即整帧拒绝（返回 `CIMC_PARSE_ASCII_INVALID_CHAR`） | `CIMC_App/Protocol/cimc_protocol.c:97-L102` |
| 输出容量 | 解码所需字节数超过输出缓冲容量即拒绝 | `CIMC_App/Protocol/cimc_protocol.c:89-L92` |
| 空白处理 | 解码器本身不处理空白，由调用方裁剪；App/BootLoader 服务层均裁剪首尾 `\r`、`\n`、空格、`\t` | `CIMC_App/Protocol/cimc_protocol.h:68`、`CIMC_App/Function/Rs485_service.c:48-L93` |
| 长度上限（协议） | 二进制帧最大 268 字节（13 + 255），对应 ASCII 536 字符；App 侧发送 ASCII 缓冲按此定义 | `CIMC_App/Protocol/cimc_protocol.h:26-L29` |
| 长度上限（RS485 通道） | 驱动帧缓冲与 DMA 发送缓冲均为 256 字节，超长帧被截断 | `CIMC_App/Driver/RS485/RS485.h:53-L54`、`CIMC_App/Driver/RS485/rs485_dma.c:183-L186` |

由此得出一个实际限制：经 USART1/RS485 通道，**ASCII 帧最长 256 字符，即二进制 128 字节，命令内容最多 115 字节**，协议层 255 字节内容上限在该通道上不可达（见第 9 节）。

## 5. 命令表

### 5.1 分派总则

1. 帧结构、长度、版本、CRC 校验通过后，先做设备 ID 过滤（本机 ID 或广播），再按帧类型过滤（`CIMC_App/Function/Rs485_service.c:1046-L1093`）。
2. 命令字按高字节分组：`0x0100` / `0x0200` / `0x0300` / `0x0400` / `0x0500` / `0x0600`，分别进入系统管理、数据、控制、阈值、升级、告警处理函数（`CIMC_App/Function/Rs485_service.c:1095-L1131`）。
3. 未命中任何已知命令的帧回通用错误帧（帧类型 `0xFF`、命令字 `0xEEEE`、内容长度 0）（`CIMC_App/Function/Rs485_service.c:1131`、`CIMC_App/Protocol/cimc_protocol.c:275-L289`）。
4. **正常应答**统一为帧类型 `0x02`、命令字与请求相同、内容 1 字节固定 `0xFF`（`CIMC_App/Protocol/cimc_protocol.h:24`、`.c:257-L273`）；带数据的命令用帧类型 `0x02` 携带各自载荷。
5. **异常应答**统一为帧类型 `0xFF`，命令字为 `0xEEEE`；`0x0501` 失败时回传原命令字 `0x0501`（`CIMC_App/Function/Rs485_service.c:1031`）。错误帧内容长度 0。
6. 除 `0x0001..0xFFFE` / 广播过滤外，代理层不再做「命令是否属于本设备型号」的判断。

### 5.2 `0x01xx` 系统管理命令

处理函数 `Rs485_Service_HandleSystemCommand`（`CIMC_App/Function/Rs485_service.c:563-L684`）。

| 命令码 | 功能 | 请求 payload（帧类型 `0x01`） | 正常响应 | 异常响应 | 源码依据 |
| --- | --- | --- | --- | --- | --- |
| `0x0101` | 软件复位 | 无（`content_len` 必须为 0） | `0x02`/`0x0101`，内容 `FF`；随后延时 300 ms 执行 `NVIC_SystemReset()` | 内容长度非 0 → `0xFF`/`0xEEEE` | `CIMC_App/Function/Rs485_service.c:576-L584`、`:16`、`:516-L526` |
| `0x0104` | 查询固件版本 | 无 | `0x02`/`0x0104`，内容 4 字节 = 主/次/修订/构建，当前为 `02 00 01 00` | 内容长度非 0 → `0xFF`/`0xEEEE` | `CIMC_App/Function/Rs485_service.c:586-L595`、`:233-L242`、`CIMC_App/Function/App_param_service.h:6-L9` |
| `0x0105` | 设置 RTC 时间 | 4 字节大端 Unix 秒 | `0x02`/`0x0105`，内容 `FF` | 长度非 4、时间无法换算为 2000–2099 年、RTC 写入失败 → `0xFF`/`0xEEEE` | `CIMC_App/Function/Rs485_service.c:597-L606`、`:491-L514`、`:404-L418` |
| `0x0106` | 查询 RTC 时间 | 无 | `0x02`/`0x0106`，内容 4 字节大端 Unix 秒 | 内容长度非 0 → `0xFF`/`0xEEEE` | `CIMC_App/Function/Rs485_service.c:608-L617`、`:473-L489` |
| `0x01A1` | 设置设备 ID | 2 字节大端设备 ID（`0x0001..0xFFFE`） | `0x02`/`0x01A1`，内容 `FF`；随后延时 700 ms 复位使新 ID 生效 | 长度非 2、ID 越界 → `0xFF`/`0xEEEE` | `CIMC_App/Function/Rs485_service.c:619-L635`、`:17` |
| `0x0111` | 查询设备 ID | 无 | `0x02`/`0x0111`，内容 2 字节大端当前 ID | 内容长度非 0 → `0xFF`/`0xEEEE` | `CIMC_App/Function/Rs485_service.c:637-L646`、`:244-L250` |
| `0x01A2` | 设置波特率 | 1 字节编码：`0x11`=4800、`0x12`=9600、`0x13`=19200、`0x14`=115200 | `0x02`/`0x01A2`，内容 `FF`（按当前波特率发出）；保存成功后延时 300 ms 复位 | 编码非法 → `0xFF`/`0xEEEE`；编码合法但保存失败时已回 OK 且不复位 | `CIMC_App/Function/Rs485_service.c:648-L667`、`CIMC_App/Function/App_param_service.c:274-L297` |
| `0x0112` | 查询波特率 | 无 | `0x02`/`0x0112`，内容 1 字节当前编码（默认 `0x13`） | 内容长度非 0 → `0xFF`/`0xEEEE` | `CIMC_App/Function/Rs485_service.c:669-L678`、`:252-L258` |

说明：`0x01A1`、`0x01A2` 的应答按**旧配置**发出，复位后才按新参数初始化串口与 ID（`CIMC_App/Function/Rs485_service.c:1134-L1138`），上位机需在收到 OK 后等待复位并切换自己的波特率/目标 ID。

### 5.3 `0x02xx` 数据命令

处理函数 `Rs485_Service_HandleDataCommand`（`CIMC_App/Function/Rs485_service.c:686-L788`）。

| 命令码 | 功能 | 请求 payload（帧类型 `0x01`） | 正常响应 | 异常响应 | 源码依据 |
| --- | --- | --- | --- | --- | --- |
| `0x0201` | 查询 CH0 当前值 | 无 | `0x02`/`0x0201`，内容 4 字节大端 float | 内容长度非 0 → `0xFF`/`0xEEEE` | `CIMC_App/Function/Rs485_service.c:698-L708` |
| `0x0202` | 查询 CH1 当前值 | 无 | `0x02`/`0x0202`，内容 4 字节大端 float | 内容长度非 0 → `0xFF`/`0xEEEE` | `CIMC_App/Function/Rs485_service.c:710-L720` |
| `0x0221` | 查询 CH2 当前值 | 无 | `0x02`/`0x0221`，内容 4 字节大端 float（当前单位为 Ω） | 内容长度非 0 → `0xFF`/`0xEEEE` | `CIMC_App/Function/Rs485_service.c:722-L732`、`:288-L295` |
| `0x0241` | 设置 CH0 变比 | 4 字节大端 float，须为有限正数且 ≤ 1000000 | `0x02`/`0x0241`，内容 `FF` | 长度非 4、数值非法 → `0xFF`/`0xEEEE` | `CIMC_App/Function/Rs485_service.c:734-L749`、`CIMC_App/Function/App_param_service.c:53-L56` |
| `0x0242` | 设置 CH1 变比 | 同上 | `0x02`/`0x0242`，内容 `FF` | 同上 | `CIMC_App/Function/Rs485_service.c:751-L766`、`CIMC_App/Function/App_param_service.c:53-L56` |
| `0x0261` | 设置自动上报间隔 | 1 字节编码：`0x01`=1 s、`0x02`=3 s、`0x03`=5 s | `0x02`/`0x0261`，内容 `FF` | 长度非 1、编码非法 → `0xFF`/`0xEEEE` | `CIMC_App/Function/Rs485_service.c:768-L782`、`CIMC_App/Function/AutoReport_service.c:14-L27` |

### 5.4 `0x03xx` 控制命令

处理函数 `Rs485_Service_HandleControlCommand`（`CIMC_App/Function/Rs485_service.c:790-L873`）。

| 命令码 | 功能 | 请求 payload（帧类型 `0x01`） | 正常响应 | 异常响应 | 源码依据 |
| --- | --- | --- | --- | --- | --- |
| `0x0301` | 设置 DAC 输出原始码 | 2 字节大端，合法范围 `0x0000..0x0FFF` | `0x02`/`0x0301`，内容 `FF`；码值同时持久化，复位后恢复输出 | 长度非 2、码值 > 4095、保存失败 → `0xFF`/`0xEEEE` | `CIMC_App/Function/Rs485_service.c:802-L835`、`CIMC_App/Driver/DAC/DAC.h:6` |
| `0x0302` | 启动自动上报 | 无 | 立即以帧类型 `0x02`/命令字 `0x0302` 发送一帧 12 字节上报载荷，之后按设定间隔周期发送；**不额外发送 OK 帧** | 内容长度非 0 → `0xFF`/`0xEEEE` | `CIMC_App/Function/Rs485_service.c:837-L845`、`CIMC_App/Function/AutoReport_service.c:64-L74` |
| `0x0303` | 停止自动上报 | 无 | 先停止上报状态机，再回 `0x02`/`0x0303`，内容 `FF` | 内容长度非 0 → `0xFF`/`0xEEEE` | `CIMC_App/Function/Rs485_service.c:847-L856` |
| `0x03AA` | 进入低功耗睡眠 | 无 | 先回 `0x02`/`0x03AA`，内容 `FF`；约 20 ms 后进入 deep sleep，RTC 闹钟 10 s 唤醒，唤醒后直发原始文本 `instrument wakeup` | 内容长度非 0 → `0xFF`/`0xEEEE` | `CIMC_App/Function/Rs485_service.c:858-L867`、`CIMC_App/Function/Sleep_service.c:7-L9`、`:132-L152`、`:119-L122` |

### 5.5 `0x04xx` 阈值命令

处理函数 `Rs485_Service_HandleThresholdCommand`（`CIMC_App/Function/Rs485_service.c:875-L948`）。

| 命令码 | 功能 | 请求 payload（帧类型 `0x01`） | 正常响应 | 异常响应 | 源码依据 |
| --- | --- | --- | --- | --- | --- |
| `0x0400` | 批量读取 CH0/CH1 阈值 | 无 | `0x02`/`0x0400`，内容 8 字节 = CH0 阈值 float(4) + CH1 阈值 float(4)，均大端 | 内容长度非 0 → `0xFF`/`0xEEEE` | `CIMC_App/Function/Rs485_service.c:897-L906`、`:875-L882` |
| `0x0401` | 读取 CH0 阈值 | 无 | `0x02`/`0x0401`，内容 4 字节大端 float | 内容长度非 0 → `0xFF`/`0xEEEE` | `CIMC_App/Function/Rs485_service.c:908-L920` |
| `0x0402` | 读取 CH1 阈值 | 无 | `0x02`/`0x0402`，内容 4 字节大端 float | 内容长度非 0 → `0xFF`/`0xEEEE` | `CIMC_App/Function/Rs485_service.c:908-L920` |
| `0x0403` | 读取 CH2 阈值 | 无 | `0x02`/`0x0403`，内容 4 字节大端 float | 内容长度非 0 → `0xFF`/`0xEEEE` | `CIMC_App/Function/Rs485_service.c:908-L920` |
| `0x0411` | 写入 CH0 阈值 | 4 字节大端 float，须有限、≥ 0、≤ 1000000 | `0x02`/`0x0411`，内容 `FF` | 长度非 4、数值非法 → `0xFF`/`0xEEEE` | `CIMC_App/Function/Rs485_service.c:922-L942`、`CIMC_App/Function/App_param_service.c:58-L61` |
| `0x0412` | 写入 CH1 阈值 | 同上 | `0x02`/`0x0412`，内容 `FF` | 同上 | `CIMC_App/Function/Rs485_service.c:922-L942` |
| `0x0413` | 写入 CH2 阈值 | 同上 | `0x02`/`0x0413`，内容 `FF` | 同上 | `CIMC_App/Function/Rs485_service.c:922-L942` |

阈值默认值三路皆为 `21.59f`（`CIMC_App/Function/App_param_service.c:11`、`:84-L86`），单位与对应通道上报值一致。

### 5.6 `0x05xx` 升级命令

App 侧处理函数 `Rs485_Service_HandleUpgradeCommand`（`CIMC_App/Function/Rs485_service.c:1007-L1039`）；`0x0502`/`0x0503` 由 BootLoader 侧处理（`CIMC_BootLoader/Function/bl_core.c:653-L661`、`:876-L890`）。

| 命令码 | 处理方 | 功能 | 请求 payload | 正常响应 | 异常响应 | 源码依据 |
| --- | --- | --- | --- | --- | --- | --- |
| `0x0501` | App | 请求复位进入 BootLoader 等待固件 | 无 | `0x02`/`0x0501`，内容 `FF`；随后延时 1500 ms 复位 | 内容长度非 0 或标志写入失败 → `0xFF`/`0x0501` | `CIMC_App/Function/Rs485_service.c:1017-L1033`、`:18` |
| `0x0502` | BootLoader | 声明开始传输固件数据包 | 无（命令帧本身空载荷；命令帧之后紧跟裸 bin 流） | 固件接收并通过校验后回 `0x02`/`0x0502`，内容 `FF` | 魔术字/向量/长度校验失败 → `0xFF`/`0x0502`，并保持升级接收窗口等待重发 | `CIMC_BootLoader/Function/bl_core.c:862-L867`、`:1288-L1310`、`:892-L914` |
| `0x0503` | BootLoader | 执行安装 | 无 | 先回 `0x02`/`0x0503`，内容 `FF`，再执行安装；成功后写 `IDLE` 并复位 | 60 s 内未收到合法 `0x0503`，或安装/参数提交失败 → `0xFF`/`0x0503` | `CIMC_BootLoader/Function/bl_core.c:876-L890`、`:1318-L1351`、`:34` |

`0x0501` 的完整流程与两条升级路径的差异见第 7 节与 `docs/ota.md`。

### 5.7 `0x06xx` 告警命令

处理函数 `Rs485_Service_HandleAlertCommand`（`CIMC_App/Function/Rs485_service.c:950-L1005`）。

| 命令码 | 功能 | 请求 payload（帧类型 `0x01`） | 正常响应 | 异常响应 | 源码依据 |
| --- | --- | --- | --- | --- | --- |
| `0x0601` | 设置告警模式 | 1 字节：`0x01`=主动上报并存储、`0x02`=仅存储 | `0x02`/`0x0601`，内容 `FF` | 长度非 1、编码非法 → `0xFF`/`0xEEEE` | `CIMC_App/Function/Rs485_service.c:960-L974`、`CIMC_App/Function/App_param_service.c:434-L437` |
| `0x0602` | 查询告警历史 | 无 | **直发原始 ASCII 文本，不做协议封装**，按最新在前逐行发送；无记录时发送 `empty` | 内容长度非 0 → `0xFF`/`0xEEEE` | `CIMC_App/Function/Rs485_service.c:976-L983`、`CIMC_App/Function/Alert_service.c:215-L233` |
| `0x0603` | 清除告警记录 | 无 | `0x02`/`0x0603`，内容 `FF` | 内容长度非 0、提交失败 → `0xFF`/`0xEEEE` | `CIMC_App/Function/Rs485_service.c:985-L999`、`CIMC_App/Function/Alert_service.c:235-L243` |

告警文本格式为 `YYYY-MM-DD HH:MM:SS | CHx | 阈值 | 实测值`，阈值与实测值各保留两位小数，行尾 `\r\n`（`CIMC_App/Function/Alert_service.c:109-L126`）；时间字符串由 `snprintf("20%02u-%02u-%02u %02u:%02u:%02u", …)` 生成（`CIMC_App/Function/Rs485_service.c:447-L471`）。告警模式为 `0x01` 时，越阈值上升沿会以同样的文本格式主动直发（`CIMC_App/Function/Alert_service.c:128-L156`），当前监测通道为 CH0/CH1（`CIMC_App/Function/Alert_service.c:209-L213`）。

## 6. 数据面

### 6.1 三通道定义与换算

| 通道 | 硬件来源 | 原始量 → 上报量 | 上报单位 | 依据 |
| --- | --- | --- | --- | --- |
| CH0 | 内部 ADC0 规则通道 `ADC_CHANNEL_10`（板载电位器） | `mV = raw × 3300 / 4095`（整数毫伏）→ 8 点滑动平均 → `上报值 = mV × CH0 变比` | mV 乘变比后的值 | `CIMC_App/Function/Adc_service.c:6-L8`、`:41-L54`、`:61-L64`、`:74-L91`、`CIMC_App/Function/Rs485_service.c:280-L283` |
| CH1 | 内部 ADC0 规则通道 `ADC_CHANNEL_11`（DAC 回读） | 同上，使用 CH1 变比 | 同上 | `CIMC_App/Function/Adc_service.c:137-L140`、`CIMC_App/Function/Rs485_service.c:284-L287` |
| CH2 | GD30AD3344 外部 ADC，SPI/DMA 读取 PT100 前端电压 | `V = int16(raw) × 量程 / 32768`（量程 `±2.048 V`）→ `R = V / 10.022 / 1.006 × 1000` | Ω（**不是** °C） | `CIMC_App/Driver/GD30AD3344/GD30AD3344.c:238-L251`、`CIMC_App/Function/Gd30ad3344_service.c:10-L33`、`:58-L81`、`CIMC_App/Function/Rs485_service.c:288-L295` |

关于 CH1：`0x0301` 是它的物理激励源，DAC 原始码会被持久化，复位后由 `Dac_Service_Init()` 恢复输出，使 CH1 回读在重启后保持同一电平（`CIMC_App/Function/Rs485_service.c:821-L830`、`CIMC_App/Function/App_param_service.h:94-L103`）。

关于 CH2：当前出口是电阻值 Ω，**温度换算未实现**，源码注释明确说明后续只需替换 `Gd30ad3344_service` 的业务换算出口（`CIMC_App/Function/Rs485_service.c:290-L294`）。协议出口未检查外部 ADC 的 `ready` 标志，底层失败时会继续上报最后一次有效值（`CIMC_App/Function/Gd30ad3344_service.c:64-L68`、`CIMC_App/Function/Rs485_service.c:288-L295`）。自动上报只包含 CH0/CH1（`CIMC_App/Function/AutoReport_service.c:41-L52`）。

内部 ADC 采样为任务内主动单通道触发读取，两路各自维护独立 8 点滑动平均与共享的环形索引（`CIMC_App/Function/Adc_service.c:126-L143`）。

### 6.2 IEEE754 大端浮点编码

编码路径：先 `memcpy` 取 float 的位模式到 `uint32_t`，再按大端写出 4 字节；解码路径相反（`CIMC_App/Protocol/cimc_protocol.c:377-L388`、`CIMC_App/Function/Rs485_service.c:260-L268`）。这么做避免了直接发送 MCU 小端 RAM 字节，也避免把浮点强转整数造成信息丢失。

自检向量（IEEE754 binary32）：

| 数值 | 位模式 | 线上字节 |
| --- | --- | --- |
| `1.0f` | `0x3F800000` | `3F 80 00 00` |
| `21.59f` | `0x41ACB852` | `41 AC B8 52` |
| `71247.0f` | `0x478B2780` | `47 8B 27 80` |
| `3.3f` | `0x40533333` | `40 53 33 33` |

该实现依赖目标平台 `float` 为 4 字节 IEEE754 单精度，源码中未见静态格式断言（待核实项，见第 9 节）。

### 6.3 自动上报载荷

`0x0302` 启动后发送的上报帧：帧类型 `0x02`、命令字 `0x0302`、内容 12 字节（`CIMC_App/Function/AutoReport_service.c:37-L55`）。

| 内容偏移 | 长度 | 字段 |
| --- | --- | --- |
| 0 | 4 | 当前 RTC 对应的 Unix 时间戳，大端 |
| 4 | 4 | CH0 上报值，IEEE754 大端 float |
| 8 | 4 | CH1 上报值，IEEE754 大端 float |

上报间隔由 `0x0261` 的编码决定（1 s / 3 s / 5 s），启动时立即发送首帧，之后按间隔发送并跳过错过的节拍（`CIMC_App/Function/AutoReport_service.c:64-L98`）。上报运行标志只存在于 RAM，掉电后回到停止状态（`CIMC_App/Function/AutoReport_service.c:11`、`:57-L62`）。

## 7. OTA 相关协议

工程内存在两条互相独立的升级链路。二者的硬件通路、包格式、落地状态与安装责任方都不同，不能混用（`docs/ota.md` 有完整状态机与安装/回滚分析）。

### 7.1 USART1 / RS485 官方升级链路

| 步骤 | 报文 / 动作 | 依据 |
| --- | --- | --- |
| 1 | App 收到 `0x0501`（帧类型 `0x01`、空载荷），把参数页 `update_flag` 置 `BL_UPDATE_FLAG_FAST_UPGRADE (0x55AA55AA)` 并整页提交 | `CIMC_App/Function/BootUpdate_service.c:208-L252`、`CIMC_App/common/bl_param.h:24` |
| 2 | App 回 `0x0501` 的 OK 帧（帧类型 `0x02`、内容 `FF`），延时 1500 ms 后软复位；同时抑制启动心跳，避免 BootLoader 文本与 OK 帧粘在同一接收窗口 | `CIMC_App/Function/Rs485_service.c:1017-L1033`、`:528-L540` |
| 3 | BootLoader 启动，读到 `FAST_UPGRADE` 后把标志提交为 `IDLE`，随后打印纯文本提示并进入 10 s 等待（“只用升级请求进入时才打印提示”由开发记忆记录，源码侧可见提示仅在 `print_banner` 为真的等待分支输出） | `CIMC_BootLoader/Function/bl_core.c:1280-L1287`、`:833-L873` |
| 4 | 等待窗口内以字节流方式扫描 ASCII-Hex 命令帧，只解码固定 26 字符（13 字节）前缀；`0x0502` 必须为空载荷命令帧，设备 ID 为当前持久化 ID 或广播 | `CIMC_BootLoader/Function/bl_core.c:569-L650`、`:30`、`:600-L620` |
| 5 | 收到 `0x0502` 后接收裸 bin 流：前 4 字节必须为魔术字 `5A A5 C3 3C`，剥离魔术字后暂存 RAM（上限 16384 字节），同步累计 payload CRC32；1.5 s 空闲或 30 s 总超时结束接收 | `CIMC_BootLoader/Function/bl_core.c:686-L735`、`:758-L830`、`:28`、`:31-L33` |
| 6 | 校验通过后按页对齐擦写下载暂存区（`0x08051000`），尾部补 `0xFF` 到页边界，并复核下载区向量表；随后持久化 `BL_UPDATE_FLAG_DOWNLOADED (0xA5A55A5A)` + `app_size` + payload CRC32，回 `0x0502` OK 帧 | `CIMC_BootLoader/Function/bl_core.c:808-L829`、`:1288-L1295`、`CIMC_App/common/bl_param.h:25` |
| 7 | 校验失败时不复位回 App：清空镜像元数据、`last_error = BL_ERR_APP2_INVALID`、`fail_counter++`，保持升级接收窗口静默等待下一个 `0x0502`；错误帧在链路静默约 100 ms 后发出，避免与仍在传输的裸字节碰撞 | `CIMC_BootLoader/Function/bl_core.c:892-L914`、`:1297-L1310`、`:737-L757` |
| 8 | 收到 `0x0503`（60 s 内）后先回 `0x0503` OK 帧，再关中断执行安装：校验下载区长度/向量/CRC32 → 备份 APP1 的擦除窗口 → 搬运并整窗编程 → 复核 APP1 前 `app_size` 字节 CRC32 → 提交 `IDLE` + `update_counter++`；失败则写 `FAILED` 并尽量用备份恢复 | `CIMC_BootLoader/Function/bl_core.c:1318-L1351`、`:1053-L1159` |
| 9 | 安装流程结束后 `NVIC_SystemReset()`；若安装或参数提交失败，先补发 `0xFF`/`0x0503` 错误帧 | `CIMC_BootLoader/Function/bl_core.c:1347-L1350` |

BootLoader 参数页写入 / 读取的波特率固定为 115200（`CIMC_BootLoader/Function/bl_core.c:40`、`:136`、`CIMC_BootLoader/Function/Function.c:9`），设备 ID 从 App 参数块继承。

### 7.2 升级期间的纯文本提示

BootLoader 在升级等待窗口以原始 ASCII（无 `A5B6` 帧封装）打印 `system init`、`Application Version 2.0.1.0`、`using command to interrupt start Application`、`wait for start Application(10s)...`，并在倒计时 7 s / 4 s / 1 s 处各再打印一次（`CIMC_BootLoader/Function/bl_core.c:838-L860`）。等待窗口内 `system init` 与版本字样会重复打印，以给上位机多次识别机会（`:845-L852`）。BootLoader 侧逐字节发送，每个字节都走一次 DE 拉高 / 等待 TC / DE 拉低（`CIMC_BootLoader/Driver/USART/usart.c:176-L190`）。

### 7.3 USART0 流式升级包头

App 侧流式通道（USART0，PA9/PA10，115200 8N1 DMA）使用 `ota_stream_header_t` 包头，其后紧跟裸 `App.bin`（`CIMC_App/Function/Usart_service.c:49-L53`、`CIMC_App/Driver/USART/USART.h:23`、`:42-L43`）。

结构为 packed，字段按打包工具（x86 小端）写入，因此**各字段为小端**（`CIMC_App/Protocol/ota_protocol.h:30-L49`、`CIMC_App/common/ota_package_config.h:4-L18`）：

| 偏移 | 长度 | 字段 | 约定值 / 含义 | 依据 |
| --- | --- | --- | --- | --- |
| `0x00` | 4 | `magic` | `0x12345678`（小端字节序 `78 56 34 12`） | `CIMC_App/common/ota_package_config.h:19` |
| `0x04` | 2 | `header_size` | 必须等于 `sizeof(ota_stream_header_t)` = 32 | `CIMC_App/Protocol/ota_protocol.c:17` |
| `0x06` | 2 | `header_version` | 必须等于 3 | `CIMC_App/common/ota_package_config.h:20` |
| `0x08` | 4 | `app_version` | 本包携带的 App 版本号（BootLoader 不读取） | `CIMC_App/Protocol/ota_protocol.h:38` |
| `0x0C` | 4 | `app_size` | 裸 bin 字节数，须非 0 且 ≤ `BL_APP2_SIZE` | `CIMC_App/Protocol/ota_protocol.c:20-L21` |
| `0x10` | 4 | `app_crc32` | 裸 bin 全量数据的 CRC32（标准 CRC-32） | `CIMC_App/Protocol/ota_protocol.h:42` |
| `0x14` | 4 | `target_addr` | 必须等于 `BL_APP1_START_ADDR` = `0x08011000` | `CIMC_App/Protocol/ota_protocol.c:18` |
| `0x18` | 4 | `image_type` | 必须等于 1（APP1） | `CIMC_App/Protocol/ota_protocol.c:19` |
| `0x1C` | 4 | `header_crc32` | 前 28 字节（`offsetof(header_crc32)`）的 CRC32 | `CIMC_App/Protocol/ota_protocol.c:26-L28` |
| `0x20` | — | 裸 bin | 从向量表开始，不含任何额外头 | `CIMC_App/common/ota_package_config.h:17` |

接收侧行为：App 把收到的数据块写入 32 KiB 环形缓冲；任务在缓冲头部按 4 字节魔术字搜索，命中后取 32 字节包头做整体校验，校验失败则丢弃 1 字节重新搜索；校验通过后按页对齐擦除下载暂存区，按最多 512 字节一块搬运并累计 CRC32；收齐后与 `app_crc32` 比较，成功后把 `update_flag` 置 `BL_UPDATE_FLAG_PENDING (0xAA55AA55)`、写入 `app_size`/`app_crc32` 并延时 100 ms 复位（`CIMC_App/Function/BootUpdate_service.c:386-L438`、`:254-L331`、`:333-L376`）。

### 7.4 两种包格式的区别

| 维度 | USART1 / RS485 官方路径 | USART0 流式路径 |
| --- | --- | --- |
| 外层 | ASCII-Hex 协议帧（`A5B6…B6A5`，CRC16-Modbus） | 无 ASCII 层，直接二进制 |
| 固件前的数据 | 先发 `0x0502` 命令帧，bin 前有 4 字节魔术字 `5A A5 C3 3C`（接收方剥离，不写入下载区） | 32 字节小端包头 `ota_stream_header_t`（写入前整体丢弃，不进入下载区） |
| 字节序 | 帧内字段大端 | 包头字段小端 |
| 落地状态 | `DOWNLOADED`，等 `0x0503` 安装 | `PENDING` |
| 安装方 | BootLoader | **未实现**：BootLoader 只处理 `FAST_UPGRADE` 与 `DOWNLOADED`，没有 `PENDING` 分支 |

因此把两种包混用（例如用 USART0 流式包往 USART1 灌，或反之）都不可用。第 9 节第 15 项与 `docs/ota.md` 第 6、11 节给出该缺口的完整分析。

另外，USART0 的 IDLE 组装分支按魔术字 `5A A5 C3 3C` 与「16 字节包头 + image_size 位于偏移 8」的假设拼帧（`CIMC_App/Driver/USART/usart_dma.c:360-L409`），与 `ota_stream_header_t`（魔术字 `0x12345678`、32 字节包头、`app_size` 位于偏移 `0x0C`）不一致。该分支在当前流式协议下不会被触发，实际接收依赖环形缓冲逐块累积；两者是否为历史遗留或有其他包版本，**待核实**。

## 8. 报文示例

以下示例均由源码算法实际计算：CRC16-Modbus 按 `CIMC_App/Protocol/cimc_protocol.c:31-L59` 逐位实现（初值 `0xFFFF`、反射多项式 `0xA001`），多字节字段按第 2 节规则以大端拼接。示例帧与 `docs/verification/协议人工核验清单.md`、`docs/verification/RS485人工核验清单.md` 中已有帧一致。

### 8.1 广播发现与心跳应答

```
广播发现（ID=FFFF）  ：A5 B6 FF FF 05 FF FF 00 02 1C 10 B6 A5  →  A5B6FFFF05FFFF00021C10B6A5
心跳应答（本机 0001）：A5 B6 00 01 05 88 88 00 02 63 A1 B6 A5  →  A5B60001058888000263A1B6A5
解析：帧头 A5B6 | ID | 类型 05（心跳）| 命令 FFFF（发现）/8888（心跳）| 长度 00 | 版本 02 | CRC | 帧尾 B6A5；
      CRC 覆盖前 9 字节，分别得 1C10、63A1。
```

### 8.2 查询 CH0 数据

```
请求（空载荷）  ：A5 B6 00 01 01 02 01 00 02 51 AB B6 A5  →  A5B60001010201000251ABB6A5
实测应答        ：A5 B6 00 01 02 02 01 04 02 47 8B 27 80 4F 1A B6 A5  →  A5B600010202010402478B27804F1AB6A5
解析：请求为类型 01、命令 0201、长度 00，CRC 覆盖前 9 字节得 51AB；
      应答为类型 02、命令 0201、长度 04，内容 47 8B 27 80 = 71247.0f，CRC 覆盖前 13 字节得 4F1A。
```

### 8.3 设置 DAC 输出与非法值错误帧

```
合法请求（DAC=2048=0x0800）：A5 B6 00 01 01 03 01 02 02 08 00 65 B9 B6 A5  →  A5B600010103010202080065B9B6A5
OK 应答                    ：A5 B6 00 01 02 03 01 01 02 FF CC 3C B6 A5  →  A5B600010203010102FFCC3CB6A5
非法请求（DAC=4096=0x1000）：A5 B6 00 01 01 03 01 02 02 10 00 65 B3 B6 A5  →  A5B600010103010202100065B3B6A5
错误应答                    ：A5 B6 00 01 FF EE EE 00 02 E0 87 B6 A5  →  A5B60001FFEEEE0002E087B6A5
解析：合法请求长度 02、内容 08 00（大端 2048），CRC 覆盖前 11 字节得 65B9；OK 帧长度 01、内容固定 FF；
      非法请求超出 0x0FFF，设备回帧类型 FF、命令字 EEEE、长度 00 的错误帧。
```

### 8.4 查询固件版本

```
请求：A5B60001010104000214BBB6A5
应答：A5 B6 00 01 02 01 04 04 02 02 00 01 00 BC 71 B6 A5  →  A5B60001020104040202000100BC71B6A5
解析：命令 0104、长度 04、内容 02 00 01 00 = 版本 2.0.1.0（主/次/修订/构建）。
```

### 8.5 官方升级报文

```
0x0501 请求进入 BootLoader：A5B60001010501000225AAB6A5
0x0501 OK                 ：A5B600010205010102FFCCB4B6A5
0x0502 开始传输（BL 侧）   ：A5B600010105020002255AB6A5
0x0502 OK（收包校验通过）  ：A5B600010205020102FF88B4B6A5
0x0503 执行安装           ：A5B600010105030002E50BB6A5
0x0503 OK                 ：A5B600010205030102FF74B5B6A5
0x0503 错误（安装失败）    ：A5B60001FF050300023122B6A5
```

`0x0502`/`0x0503` 命令帧之后紧跟的是裸 bin 字节流而非 ASCII-Hex；上位机把命令帧与首批 bin 字节连发时，BootLoader 只解码前 26 个 ASCII 字符作为命令帧，其余字节按裸流暂存消费（`CIMC_BootLoader/Function/bl_core.c:600-L620`）。

### 8.6 自动上报帧

时间戳 `0x5F5E1000`、CH0 = `1.0`、CH1 = `3.3`（时间戳与测量值为示例数据，不是实测记录）：

```
A5 B6 00 01 02 03 02 0C 02 5F 5E 10 00 3F 80 00 00 40 53 33 33 8A 8E B6 A5  →  A5B600010203020C025F5E10003F800000405333338A8EB6A5
解析：类型 02、命令 0302、长度 0C（12）、版本 02；内容 = 时间戳(4) + CH0 float(4) + CH1 float(4)；CRC 覆盖前 21 字节得 8A8E。
```

### 8.7 OTA 流式包头示例（格式演示）

`app_crc32` 为占位值 `0x00000000`，真实值由打包工具对裸 bin 计算：

```
前 28 字节：78 56 34 12 | 20 00 | 03 00 | 00 00 01 00 | 04 19 00 00 | 00 00 00 00 | 00 10 01 08 | 01 00 00 00
对应字段  ：magic=0x12345678 | header_size=32 | header_version=3 | app_version=0x00010000
            app_size=6404 | app_crc32=0（占位）| target_addr=0x08011000 | image_type=1
header_crc32 = 0xA03FC921（对前 28 字节做标准 CRC-32）
```

## 9. 协议层已知边界与待改进项

以下均为当前实现的真实状态，不是改进承诺。

| # | 现状 | 说明与依据 |
| --- | --- | --- |
| 1 | 接收侧只有单帧缓冲、没有多帧队列 | IDLE 中断把数据写进唯一的 `s_frame_buffer` 并置 `s_frame_ready`；若主循环尚未取走，下一次 IDLE 会直接覆盖前一帧。`CIMC_App/Driver/RS485/rs485_dma.c:37-L43`、`:304-L319`、`:243-L274` |
| 2 | 分帧完全依赖 IDLE 间隙 | 连续无间隙的字节流会被当成一帧并可能被截断到 256 字节；没有 TCP 式的粘包/半包增量恢复，也不做重传。`CIMC_App/Driver/RS485/rs485_dma.c:288-L328`、`CIMC_App/Function/Rs485_service.c:1155-L1198` |
| 3 | DMA 发送等待无超时 | 发送路径以 `while` 空等 FTF 与 USART TC，没有超时或错误恢复；DMA/时钟异常时会一直停在该循环。`CIMC_App/Driver/RS485/rs485_dma.c:221-L229` |
| 4 | RS485 通道实际帧长上限低于协议上限 | 驱动收发缓冲均为 256 字节，发送超过 256 字节直接截断，接收超过 256 字节同样截断；协议层允许的 255 字节内容（268 字节帧、536 字符 ASCII）在该通道不可达，实际内容上限约 115 字节。`CIMC_App/Driver/RS485/RS485.h:53-L54`、`CIMC_App/Driver/RS485/rs485_dma.c:183-L186`、`:307-L310`、`CIMC_App/Protocol/cimc_protocol.h:26-L29` |
| 5 | 命令帧长度校验为精确比较，但 BootLoader 侧只看固定前缀 | App 各命令均用 `content_len != n` 精确判断；BootLoader 的 `0x0502`/`0x0503` 扫描只解码前 26 个 ASCII 字符，其后所有非填充字节都被当作裸固件流处理，因此命令帧尾部可以携带任意字节（这是为粘连帧刻意设计的行为，但同时也意味着命令帧边界不被严格强制）。`CIMC_App/Function/Rs485_service.c:576-L1017`、`CIMC_BootLoader/Function/bl_core.c:600-L620` |
| 6 | 错误应答不带原因码 | 通用错误帧只有帧类型 `0xFF` 与命令字，可能原因包括长度错、数值越界、Flash 写入失败、时间越界等，上位机无法从帧内容区分。`CIMC_App/Protocol/cimc_protocol.c:275-L289`、`CIMC_App/Function/Rs485_service.c:576-L1131` |
| 7 | App 侧参数写入失败仍回 OK | `AppParam_Service_Commit()` 调用的 Flash 接口无返回值，函数本身直接返回 1，因此 OK 应答不代表持久化成功（对比：`0x0501` 用的 `BootUpdate_Service_RequestEnterBootloader()` 检查了写入结果并会在失败时回错误帧）。`CIMC_App/Function/App_param_service.c:196-L226`、`CIMC_App/Function/Rs485_service.c:1024-L1032` |
| 8 | `0x01A2` 合法但保存失败时已回 OK 且不复位 | 处理顺序是先校验编码、再回 OK、最后保存并安排复位；保存失败只表现为不复位，不再补错误帧。`CIMC_App/Function/Rs485_service.c:648-L667` |
| 9 | 接收侧回灌抑制是启发式窗口 | 直发原始文本后会开启约 500 ms、最多 255 次的抑制计数，期间解析失败会被静默丢弃；若这期间确实有上位机发来的非法帧，也不会回错误帧。`CIMC_App/Function/Rs485_service.c:204-L231`、`:1173-L1178` |
| 10 | 自动上报期间几乎不响应任何命令 | 除 `0x0303` 外的命令帧与解析错误一律静默，因此上报期间无法查询数据或改参数，上位机必须显式停止上报。`CIMC_App/Function/Rs485_service.c:1068-L1075`、`:1179-L1193` |
| 11 | 广播命令会引发多机同时应答 | 广播帧被所有节点受理并按本机 ID 应答，没有随机退避、应答延时或冲突检测；总线电气层面的仲裁与终端匹配未在本次核对范围内（待核实）。`CIMC_App/Function/Rs485_service.c:162-L171`、`:173-L185` |
| 12 | 时间戳范围限制导致部分 `0x0105` 失败 | Unix 秒换算只支持 2000–2099 年，超出范围的设置请求返回错误帧；RTC 未初始化时的行为未在源码中单独判定（待核实）。`CIMC_App/Function/Rs485_service.c:404-L418`、`:491-L514` |
| 13 | CH2 出口为电阻而非温度，且未做失效隔离 | `0x0221` 返回 Ω；温度换算未实现；外部 ADC `ready` 为 0 时协议出口仍返回上一次有效值。`CIMC_App/Function/Rs485_service.c:288-L295`、`CIMC_App/Function/Gd30ad3344_service.c:64-L68` |
| 14 | 浮点编码依赖平台 IEEE754 单精度 | `WriteFloatBe` 用 `memcpy` 取位模式，未做 `sizeof(float) == 4` 或格式的静态断言（待核实：是否需要补充编译期断言）。`CIMC_App/Protocol/cimc_protocol.c:377-L388` |
| 15 | USART0 流式路径的包格式不一致且未闭环 | IDLE 组装分支按 `5A A5 C3 3C` + 16 字节包头判断，与 `ota_stream_header_t`（`0x12345678`、32 字节包头）不一致；且 BootLoader 无 `PENDING` 安装分支，`PENDING` 状态会一直保留。`CIMC_App/Driver/USART/usart_dma.c:360-L409`、`CIMC_App/Function/BootUpdate_service.c:167-L206`、`CIMC_BootLoader/Function/bl_core.c:1280`、`:1318` |
| 16 | 无认证、无签名、无防回退 | CRC16/CRC32 只能检错；没有消息认证、固件签名或版本回退保护，`OTA_PACKAGE_APP_VERSION` 只用于打包填写，BootLoader 不读取比较。`CIMC_App/Protocol/cimc_protocol.c:31-L59`、`:291-L328`、`CIMC_App/common/ota_package_config.h:21` |
| 17 | 无序号、无确认、无重传机制 | 协议没有帧序号、分片、ACK/NAK 语义与超时重传；`0x0502` 的固件流靠「空闲超时」判定结束，丢字节只能靠上层重新发起升级。`CIMC_BootLoader/Function/bl_core.c:31-L34`、`:779-L799` |
| 18 | 上位机可见性缺口 | `0x0503` OK 之后是一段关中断的 Flash 操作静默窗口，总线上无法区分「正在安装」与「已卡死」；解析失败与命令被忽略（未寻址、上报期间、回灌抑制）在总线上都表现为无应答。`CIMC_BootLoader/Function/bl_core.c:1338-L1350`、`CIMC_App/Function/Rs485_service.c:1048-L1075` |

## 10. 依据

本文实际读取并据此撰写的文件：

- `CIMC_App/Protocol/cimc_protocol.h`、`CIMC_App/Protocol/cimc_protocol.c`
- `CIMC_BootLoader/Protocol/cimc_protocol.h`、`CIMC_BootLoader/Protocol/cimc_protocol.c`（与 App 侧逐字节一致，SHA-256 相同）
- `CIMC_App/Protocol/ota_protocol.h`、`CIMC_App/Protocol/ota_protocol.c`
- `CIMC_App/common/ota_package_config.h`、`CIMC_App/common/bl_partition.h`、`CIMC_App/common/bl_param.h`
- `CIMC_App/Function/Rs485_service.h`、`CIMC_App/Function/Rs485_service.c`
- `CIMC_App/Function/App_param_service.h`、`CIMC_App/Function/App_param_service.c`
- `CIMC_App/Function/AutoReport_service.c`、`CIMC_App/Function/Alert_service.c`
- `CIMC_App/Function/Adc_service.c`、`CIMC_App/Function/Gd30ad3344_service.c`、`CIMC_App/Function/Dac_service.h`
- `CIMC_App/Function/BootUpdate_service.c`、`CIMC_App/Function/Sleep_service.c`、`CIMC_App/Function/Usart_service.c`
- `CIMC_App/Driver/RS485/RS485.h`、`CIMC_App/Driver/RS485/RS485.c`、`CIMC_App/Driver/RS485/rs485_dma.c`、`CIMC_App/Driver/RS485/rs485_idle.c`（部分）
- `CIMC_App/Driver/USART/USART.h`、`CIMC_App/Driver/USART/usart_dma.c`（部分）
- `CIMC_App/Driver/GD30AD3344/GD30AD3344.c`（部分）、`CIMC_App/Driver/DAC/DAC.h`（部分）
- `CIMC_BootLoader/Function/bl_core.c`、`CIMC_BootLoader/Function/Function.c`
- `CIMC_BootLoader/Driver/USART/usart.c`、`CIMC_BootLoader/Driver/USART/usart.h`
- `docs/ota.md`、`docs/verification/协议人工核验清单.md`、`docs/verification/RS485人工核验清单.md`（示例帧交叉核对）
- `docs/dev-notes/PROJECT_MEMORY.md`（开发记忆，仅用于交叉参考现象与决策来源；与源码冲突时以源码为准。本文多处引用该文件的地方已标注为「开发记忆记录」或「待核实」）
- 作者本人的项目问答稿（工程外资料，非本仓库文件）：逐条回源码核对过，用作事实交叉核对
- 报文示例的 CRC16 与 CRC32 由 Python 按上述源码算法复算，结果与 `docs/verification/` 下已有示例帧一致
