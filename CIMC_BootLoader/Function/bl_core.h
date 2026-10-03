#ifndef BL_CORE_H
#define BL_CORE_H

void bootloader_run(void);
void bl_log_dump_uart(void);
uint32_t bootloader_get_upgrade_baudrate(void);

#endif /* BL_CORE_H */
