/************************************************************
 * 版权所有：2025 CIMC Copyright
 * 文件名称：SPI_FLASH.c
 * 作    者：Jianchuan Wang / Tao Liu @ GigaDevice
 * 维护作者：Caden / Codex
 * 版    本：V0.02
 * 说    明：板载 SPI Flash 底层驱动，适配 GD32F470 的 SPI0 + PA15 片选。
 ************************************************************/

#include "SPI_FLASH.h"

/* 范围擦除时需要先把整扇区搬到 RAM 中，下面这个静态缓冲区就是给这一步准备的。 */
/* 之所以放成静态全局，而不是函数局部变量，是为了避免 4KB 缓冲区把 1KB 主栈直接撑爆。 */
static uint8_t s_spi_flash_sector_buffer[SPI_FLASH_SECTOR_SIZE];

/* 0x02 是页编程命令，用来把最多 256 字节数据写进当前页。 */
#define SPI_FLASH_CMD_WRITE  ((uint8_t)0x02U)

/* 0x01 是写状态寄存器命令，本工程当前没有直接用到，但原模板保留了它。 */
#define SPI_FLASH_CMD_WRSR   ((uint8_t)0x01U)

/* 0x06 是写使能命令，任何擦写前都必须先发它。 */
#define SPI_FLASH_CMD_WREN   ((uint8_t)0x06U)

/* 0x03 是标准读命令，适合最基础、最直接的连续读取。 */
#define SPI_FLASH_CMD_READ   ((uint8_t)0x03U)

/* 0x05 是读状态寄存器命令，主要拿来轮询忙标志位。 */
#define SPI_FLASH_CMD_RDSR   ((uint8_t)0x05U)

/* 0x9F 是 JEDEC ID 读取命令，常用来确认器件是否在线。 */
#define SPI_FLASH_CMD_RDID   ((uint8_t)0x9FU)

/* 0x20 是 4KB 扇区擦除命令，是最常用的擦除粒度。 */
#define SPI_FLASH_CMD_SE     ((uint8_t)0x20U)

/* 0xC7 是整片擦除命令，会把整颗 Flash 全部恢复成 0xFF。 */
#define SPI_FLASH_CMD_BE     ((uint8_t)0xC7U)

/* 状态寄存器 bit0 是 WIP 标志位，1 表示芯片内部仍在忙。 */
#define SPI_FLASH_WIP_FLAG   ((uint8_t)0x01U)

/* 读数据时主机也必须送字节来产生时钟，这里统一用 0xA5 作为占位值。 */
#define SPI_FLASH_DUMMY_BYTE ((uint8_t)0xA5U)

/**
 * @brief 把任意地址规整到它所在扇区的首地址。
 * @param address Flash 内部任意地址。
 * @return uint32_t 该地址所属 4KB 扇区的起始地址。
 */
static uint32_t spi_flash_get_sector_base(uint32_t address)
{
    return (address - (address % SPI_FLASH_SECTOR_SIZE)); /* 用“减去扇区内偏移”的方式回到扇区首地址。 */
}

/**
 * @brief 取两个 32 位无符号整数中的较小值。
 * @param value_a 第一个待比较值。
 * @param value_b 第二个待比较值。
 * @return uint32_t 两者中较小的那个值。
 */
static uint32_t spi_flash_min_u32(uint32_t value_a, uint32_t value_b)
{
    return ((value_a < value_b) ? value_a : value_b); /* 拆成独立小函数后，分页和分扇区逻辑会更容易读懂。 */
}

/**
 * @brief 按 JEDEC NOR Flash 协议发送 24 位地址。
 * @param address 需要发出的 Flash 内部地址。
 * @return void 无返回值。
 */
static void spi_flash_send_address(uint32_t address)
{
    spi_flash_send_byte((uint8_t)((address & 0x00FF0000UL) >> 16)); /* 第 1 个字节先发高 8 位，因为总线要求高位先出。 */
    spi_flash_send_byte((uint8_t)((address & 0x0000FF00UL) >> 8));  /* 第 2 个字节发送中间 8 位，地址还在继续拼接。 */
    spi_flash_send_byte((uint8_t)(address & 0x000000FFUL));         /* 第 3 个字节发送低 8 位，24 位地址到这里才完整。 */
}

/**
 * @brief 初始化板载 SPI Flash 所依赖的 GPIO 和 SPI0 外设。
 * @param void 无入参。
 * @return void 无返回值。
 */
void spi_flash_init(void)
{
    spi_parameter_struct spi_init_struct; /* 先准备一个 SPI 参数结构体，后面逐项把工作模式填进去。 */

    rcu_periph_clock_enable(RCU_GPIOA); /* 片选脚在 GPIOA，所以必须先打开 GPIOA 时钟。 */
    rcu_periph_clock_enable(RCU_GPIOB); /* SPI0 的 SCK/MISO/MOSI 走 GPIOB，也要先给它供时钟。 */
    rcu_periph_clock_enable(RCU_SPI0);  /* 没有 SPI0 外设时钟，后面的 SPI 配置都不会真正生效。 */

    gpio_af_set(GPIOB, GPIO_AF_5, GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5); /* 把 PB3、PB4、PB5 复用到 AF5，让它们连到 SPI0。 */
    gpio_mode_set(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5); /* 把这三根线切成复用功能模式。 */
    gpio_output_options_set(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_25MHZ, GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5); /* 给 SPI 总线引脚配置推挽输出能力。 */

    gpio_mode_set(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO_PIN_15); /* PA15 不走硬件 NSS，而是作为普通输出脚手动控制片选。 */
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_15); /* 片选脚也配置成推挽输出，保证拉高拉低都干净利落。 */

    SPI_FLASH_CS_HIGH(); /* 初始化时先让片选保持无效，避免上电抖动误触发 Flash 命令。 */

    spi_struct_para_init(&spi_init_struct); /* 先把结构体清成库默认值，避免未初始化字段留下脏数据。 */
    spi_init_struct.trans_mode = SPI_TRANSMODE_FULLDUPLEX; /* SPI Flash 读和写都依赖全双工移位，所以这里选全双工。 */
    spi_init_struct.device_mode = SPI_MASTER; /* MCU 是主机，Flash 是从机，因此 SPI0 必须配置成主模式。 */
    spi_init_struct.frame_size = SPI_FRAMESIZE_8BIT; /* 常规 JEDEC 命令按字节传输，所以这里用 8bit 帧格式。 */
    spi_init_struct.clock_polarity_phase = SPI_CK_PL_LOW_PH_1EDGE; /* 这里对应 SPI Mode 0，也是大多数 Nor Flash 默认兼容模式。 */
    spi_init_struct.nss = SPI_NSS_SOFT; /* 片选已经由 PA15 手动控制，所以 SPI 内部只需要软件 NSS。 */
    spi_init_struct.prescale = SPI_PSC_8; /* 先用分频 8 的稳妥时钟跑通驱动，后续再按器件规格提速。 */
    spi_init_struct.endian = SPI_ENDIAN_MSB; /* 命令和地址都要求高位先发，因此这里选择 MSB First。 */

    spi_init(SPI0, &spi_init_struct); /* 把上面准备好的参数真正写入 SPI0 外设寄存器。 */
    spi_enable(SPI0);                 /* 最后使能 SPI0，驱动到这里才真正具备收发能力。 */
}

/**
 * @brief 擦除目标地址所在的整个 4KB 扇区。
 * @param sector_addr 目标扇区内的任意地址。
 * @return void 无返回值。
 */
void spi_flash_sector_erase(uint32_t sector_addr)
{
    uint32_t sector_base = spi_flash_get_sector_base(sector_addr); /* 不管用户传的是扇区内哪个地址，这里都先规整到扇区首地址。 */

    spi_flash_write_enable(); /* 擦除属于修改类操作，所以第一步永远是先发写使能。 */

    SPI_FLASH_CS_LOW();                    /* 片选拉低，告诉 Flash 现在开始接收一条完整命令。 */
    spi_flash_send_byte(SPI_FLASH_CMD_SE); /* 发出 4KB 扇区擦除命令 0x20。 */
    spi_flash_send_address(sector_base);   /* 紧接着发送目标扇区的 24 位地址。 */
    SPI_FLASH_CS_HIGH();                   /* 命令和地址发完后释放片选，让 Flash 开始内部擦除。 */

    spi_flash_wait_for_write_end(); /* 扇区擦除不是瞬间完成，所以必须轮询直到芯片不忙。 */
}

/**
 * @brief 擦除任意长度的数据区间，并保护同扇区内未命中的数据。
 * @param sector_addr 擦除起始地址，可以不按扇区对齐。
 * @param num_byte_to_erase 需要擦除的总字节数。
 * @return void 无返回值。
 */
void spi_flash_buffer_erase(uint32_t sector_addr, uint32_t num_byte_to_erase)
{
    uint32_t current_addr = sector_addr;       /* current_addr 表示“这轮准备处理到哪里了”。 */
    uint32_t remain_bytes = num_byte_to_erase; /* remain_bytes 表示“还有多少字节没处理完”。 */

    if (num_byte_to_erase == 0U) /* 长度为 0 时直接返回，避免做无意义擦写。 */
    {
        return; /* 空操作不应该真的去访问 Flash。 */
    }

    while (remain_bytes > 0U) /* 只要还有剩余长度，就一扇区一扇区往前推进。 */
    {
        uint32_t sector_base = spi_flash_get_sector_base(current_addr); /* 先找到当前地址所在扇区的首地址。 */
        uint32_t offset_in_sector = current_addr - sector_base;         /* 再算出当前地址在这个扇区内偏了多少。 */
        uint32_t erasable_bytes = SPI_FLASH_SECTOR_SIZE - offset_in_sector; /* 这个扇区从当前地址开始，最多还能覆盖多少字节。 */
        uint32_t chunk_size = spi_flash_min_u32(remain_bytes, erasable_bytes); /* 本轮真实处理长度取“剩余长度”和“扇区剩余容量”的较小值。 */

        if ((offset_in_sector == 0U) && (chunk_size == SPI_FLASH_SECTOR_SIZE)) /* 如果这次刚好完整命中整扇区，就没必要先读再写。 */
        {
            spi_flash_sector_erase(sector_base); /* 直接整扇区擦除，最省时也最简单。 */
        }
        else /* 如果只命中了扇区的一部分，就必须先保护扇区内其它旧数据。 */
        {
            spi_flash_buffer_read(s_spi_flash_sector_buffer, sector_base, (uint16_t)SPI_FLASH_SECTOR_SIZE); /* 先把整扇区旧内容完整搬到 RAM 缓冲区。 */
            memset(&s_spi_flash_sector_buffer[offset_in_sector], 0xFF, chunk_size); /* 只把目标范围改成 0xFF，表示这些字节将被“擦空”。 */
            spi_flash_sector_erase(sector_base); /* 然后执行真正的整扇区硬件擦除。 */
            spi_flash_buffer_write(s_spi_flash_sector_buffer, sector_base, SPI_FLASH_SECTOR_SIZE); /* 最后把需要保留的数据连同空洞一起整扇区写回。 */
        }

        current_addr += chunk_size; /* 地址向后推进，准备处理下一段尚未擦除的范围。 */
        remain_bytes -= chunk_size; /* 同时扣减剩余长度，直到整段区间全部处理完成。 */
    }
}

/**
 * @brief 擦除整片 SPI Flash。
 * @param void 无入参。
 * @return void 无返回值。
 */
void spi_flash_bulk_erase(void)
{
    spi_flash_write_enable(); /* 整片擦除同样属于修改操作，所以也必须先写使能。 */

    SPI_FLASH_CS_LOW();                    /* 片选拉低，开始一条新的命令事务。 */
    spi_flash_send_byte(SPI_FLASH_CMD_BE); /* 发送整片擦除命令。 */
    SPI_FLASH_CS_HIGH();                   /* 命令发完后释放片选，让 Flash 进入内部整片擦除流程。 */

    spi_flash_wait_for_write_end(); /* 整片擦除耗时最长，必须等到忙标志清零。 */
}

/**
 * @brief 在同一页内连续写入若干字节。
 * @param pbuffer 待写入数据缓冲区。
 * @param write_addr 起始写地址，不能跨页。
 * @param num_byte_to_write 本次写入的字节数。
 * @return void 无返回值。
 */
void spi_flash_page_write(const uint8_t *pbuffer, uint32_t write_addr, uint16_t num_byte_to_write)
{
    if ((pbuffer == NULL) || (num_byte_to_write == 0U)) /* 指针为空或者长度为 0 时，没必要继续走总线流程。 */
    {
        return; /* 直接返回，避免产生无效事务。 */
    }

    spi_flash_write_enable(); /* 每次页编程前都要重新把 WEL 位置 1。 */

    SPI_FLASH_CS_LOW();                       /* 片选拉低，本次页编程事务开始。 */
    spi_flash_send_byte(SPI_FLASH_CMD_WRITE); /* 先发页编程命令 0x02。 */
    spi_flash_send_address(write_addr);       /* 再发 24 位起始地址，告诉 Flash 从哪里开始写。 */

    while (num_byte_to_write > 0U) /* 剩多少字节，就继续送多少字节。 */
    {
        spi_flash_send_byte(*pbuffer); /* 取出当前缓冲区的 1 个字节，送到 SPI 总线。 */
        pbuffer++;                     /* RAM 指针后移，为下一字节做准备。 */
        num_byte_to_write--;          /* 剩余待写长度减 1，直到这页写完。 */
    }

    SPI_FLASH_CS_HIGH(); /* 只有片选被释放后，Flash 才会真正开始内部页编程。 */

    spi_flash_wait_for_write_end(); /* 页编程也不是瞬间完成，所以这里继续等待芯片空闲。 */
}

/**
 * @brief 自动拆页并写入任意长度的数据。
 * @param pbuffer 待写入数据缓冲区。
 * @param write_addr Flash 内部起始地址。
 * @param num_byte_to_write 需要写入的总长度。
 * @return void 无返回值。
 */
void spi_flash_buffer_write(const uint8_t *pbuffer, uint32_t write_addr, uint32_t num_byte_to_write)
{
    uint32_t current_addr = write_addr;       /* current_addr 始终指向本轮要写到的 Flash 地址。 */
    uint32_t remain_bytes = num_byte_to_write; /* remain_bytes 用来记录还有多少数据没有写完。 */

    if ((pbuffer == NULL) || (num_byte_to_write == 0U)) /* 空指针或空长度都直接视为无效写请求。 */
    {
        return; /* 不进入分页写流程，避免无意义操作。 */
    }

    while (remain_bytes > 0U) /* 只要还有剩余数据，就循环拆页写。 */
    {
        uint32_t offset_in_page = current_addr % SPI_FLASH_PAGE_SIZE; /* 先算出当前地址在页内的偏移。 */
        uint32_t writable_bytes = SPI_FLASH_PAGE_SIZE - offset_in_page; /* 再算出当前页还剩多少可写空间。 */
        uint32_t chunk_size = spi_flash_min_u32(remain_bytes, writable_bytes); /* 本轮最多只能写“剩余长度”和“本页余量”中的较小者。 */

        spi_flash_page_write(pbuffer, current_addr, (uint16_t)chunk_size); /* 把这一小段交给单页写函数完成。 */

        current_addr += chunk_size; /* Flash 目标地址向后推进。 */
        pbuffer += chunk_size;      /* RAM 数据指针同步后移。 */
        remain_bytes -= chunk_size; /* 未完成长度同步扣减。 */
    }
}

/**
 * @brief 从 Flash 中读取连续数据到 RAM 缓冲区。
 * @param pbuffer 接收读取结果的缓冲区。
 * @param read_addr Flash 内部起始地址。
 * @param num_byte_to_read 需要读取的总长度。
 * @return void 无返回值。
 */
void spi_flash_buffer_read(uint8_t *pbuffer, uint32_t read_addr, uint32_t num_byte_to_read)
{
    if ((pbuffer == NULL) || (num_byte_to_read == 0U)) /* 没有目标缓冲区或者长度为 0 时，不需要占用 SPI 总线。 */
    {
        return; /* 直接退出，保持接口行为可预测。 */
    }

    SPI_FLASH_CS_LOW();                    /* 拉低片选，开始一次标准读事务。 */
    spi_flash_send_byte(SPI_FLASH_CMD_READ); /* 发送 0x03 标准读命令。 */
    spi_flash_send_address(read_addr);     /* 紧接着发送起始地址。 */

    while (num_byte_to_read > 0U) /* 要读多少字节，就循环产生多少次时钟。 */
    {
        *pbuffer = spi_flash_send_byte(SPI_FLASH_DUMMY_BYTE); /* 主机发占位字节，Flash 同步回传 1 个真实数据字节。 */
        pbuffer++;                                            /* 缓冲区指针后移，准备接收下一个字节。 */
        num_byte_to_read--;                                   /* 剩余待读字节数减 1。 */
    }

    SPI_FLASH_CS_HIGH(); /* 读取结束后释放片选，结束本次事务。 */
}

/**
 * @brief 读取当前 Flash 的 JEDEC ID。
 * @param void 无入参。
 * @return uint32_t 24 位 JEDEC ID。
 */
uint32_t spi_flash_read_id(void)
{
    uint32_t manufacturer_id = 0U; /* 第 1 个字节通常是厂商 ID。 */
    uint32_t memory_type = 0U;     /* 第 2 个字节通常表示器件类型。 */
    uint32_t memory_capacity = 0U; /* 第 3 个字节通常表示容量编码。 */

    SPI_FLASH_CS_LOW();                     /* 拉低片选，开始 RDID 命令。 */
    spi_flash_send_byte(SPI_FLASH_CMD_RDID); /* 发送 JEDEC 读 ID 指令 0x9F。 */

    manufacturer_id = spi_flash_send_byte(SPI_FLASH_DUMMY_BYTE); /* 继续送时钟，读回厂商 ID。 */
    memory_type = spi_flash_send_byte(SPI_FLASH_DUMMY_BYTE);     /* 再读回器件类型字节。 */
    memory_capacity = spi_flash_send_byte(SPI_FLASH_DUMMY_BYTE); /* 最后读回容量编码字节。 */

    SPI_FLASH_CS_HIGH(); /* 三个字节读完后释放片选，结束识别事务。 */

    return ((manufacturer_id << 16) | (memory_type << 8) | memory_capacity); /* 把 3 个字节拼成一个 24 位整型返回。 */
}

/**
 * @brief 启动一次连续读取序列，但不立即释放片选。
 * @param read_addr 连续读取的起始地址。
 * @return void 无返回值。
 */
void spi_flash_start_read_sequence(uint32_t read_addr)
{
    SPI_FLASH_CS_LOW();                    /* 片选保持有效，让本次连续读事务一直处于打开状态。 */
    spi_flash_send_byte(SPI_FLASH_CMD_READ); /* 先发标准读命令。 */
    spi_flash_send_address(read_addr);     /* 再发起始地址，后续就可以一直逐字节往外读。 */
}

/**
 * @brief 在已建立好的连续读事务中继续读取 1 个字节。
 * @param void 无入参。
 * @return uint8_t 当前读出的 1 个字节。
 */
uint8_t spi_flash_read_byte(void)
{
    return spi_flash_send_byte(SPI_FLASH_DUMMY_BYTE); /* 读 1 字节的本质就是继续发占位字节来换取从机回传。 */
}

/**
 * @brief 通过 SPI0 发送 1 个字节，并返回同时收到的 1 个字节。
 * @param byte 需要发出的字节。
 * @return uint8_t 本次同步收发得到的返回字节。
 */
uint8_t spi_flash_send_byte(uint8_t byte)
{
    while (RESET == spi_i2s_flag_get(SPI0, SPI_FLAG_TBE)) /* 等到发送缓冲区空出来，才能安全写入新字节。 */
    {
        /* 这里刻意保持空循环，因为底层 SPL 驱动就是通过标志位轮询工作的。 */
    }

    spi_i2s_data_transmit(SPI0, byte); /* 把要发送的字节写入数据寄存器，硬件随后开始真正移位发送。 */

    while (RESET == spi_i2s_flag_get(SPI0, SPI_FLAG_RBNE)) /* 等到接收缓冲区非空，说明这次同步收发已经完成。 */
    {
        /* SPI 总线天生全双工，所以只有等收到返回值后，这次收发才算真正结束。 */
    }

    return (uint8_t)spi_i2s_data_receive(SPI0); /* 读出这次收发得到的返回字节，并按 8 位类型返回。 */
}

/**
 * @brief 通过 SPI0 发送 1 个半字，并返回同时收到的 1 个半字。
 * @param half_word 需要发出的 16 位数据。
 * @return uint16_t 本次 16 位传输收到的数据。
 */
uint16_t spi_flash_send_halfword(uint16_t half_word)
{
    while (RESET == spi_i2s_flag_get(SPI0, SPI_FLAG_TBE)) /* 先等发送缓冲区可写。 */
    {
        /* 继续轮询，直到硬件允许装载新的 16 位数据。 */
    }

    spi_i2s_data_transmit(SPI0, half_word); /* 把 16 位数据写入 SPI 数据寄存器。 */

    while (RESET == spi_i2s_flag_get(SPI0, SPI_FLAG_RBNE)) /* 再等接收缓冲区拿到同步返回值。 */
    {
        /* 不做其它操作，只等待当前这次传输结束。 */
    }

    return spi_i2s_data_receive(SPI0); /* 返回这次 16 位收发得到的结果。 */
}

/**
 * @brief 发送写使能命令。
 * @param void 无入参。
 * @return void 无返回值。
 */
void spi_flash_write_enable(void)
{
    SPI_FLASH_CS_LOW();                      /* 拉低片选，开始一条独立的写使能命令。 */
    spi_flash_send_byte(SPI_FLASH_CMD_WREN); /* 发送 0x06，把 Flash 内部 WEL 位置 1。 */
    SPI_FLASH_CS_HIGH();                     /* 释放片选，写使能命令到此结束。 */
}

/**
 * @brief 轮询状态寄存器，等待写入或擦除结束。
 * @param void 无入参。
 * @return void 无返回值。
 */
void spi_flash_wait_for_write_end(void)
{
    uint8_t flash_status = 0U; /* 用来保存每轮从状态寄存器读回来的值。 */

    SPI_FLASH_CS_LOW();                     /* 拉低片选，开始连续读取状态寄存器。 */
    spi_flash_send_byte(SPI_FLASH_CMD_RDSR); /* 发送 0x05 读状态寄存器命令。 */

    do /* 只要 WIP 还是 1，就说明 Flash 仍在忙。 */
    {
        flash_status = spi_flash_send_byte(SPI_FLASH_DUMMY_BYTE); /* 继续送时钟，把最新状态字节读回来。 */
    } while ((flash_status & SPI_FLASH_WIP_FLAG) != 0U); /* bit0 为 1 就继续等，为 0 才说明内部操作结束。 */

    SPI_FLASH_CS_HIGH(); /* 忙标志清零后释放片选，结束这次轮询会话。 */
}
