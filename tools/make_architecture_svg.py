# -*- coding: utf-8 -*-
"""生成 README 用的系统架构 SVG（中文 / 英文 × 浅色 / 深色，共 4 份）。

用法：  python tools/make_architecture_svg.py
输出：  assets/architecture-{zh,en}-{light,dark}.svg

图由本脚本生成，不手工编辑 SVG；改图请改 do_layout/build 里的坐标与文案表，
然后重新运行本脚本，并用浏览器渲染确认（Chrome --headless=new --screenshot）。
"""
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'assets')
W, H = 1240, 950
FONT = "Segoe UI, Noto Sans SC, PingFang SC, Microsoft YaHei, Arial, sans-serif"

LIGHT = dict(
    page='#ffffff', frame='#d8e4f5', grid='#b8cce4',
    mcu_fill='url(#mcuGrad)', side_fill='#f7fbff', side_stroke='#cfe0f5',
    text='#16283f', muted='#5d7290', blue='#2f6fd0', blue_soft='#e8f1ff',
    teal='#0f8f83', amber='#c2760c', purple='#7b5bd6', green='#2f8f4e',
    arrow='#7d94b3',
    p=['#dce9ff', '#fdf0d5', '#e2f6e6', '#eef1f5', '#f0eafd'],
    pb=['#2f6fd0', '#c2760c', '#2f8f4e', '#8ba0b8', '#7b5bd6'],
)
DARK = dict(
    page='#0d1117', frame='#30363d', grid='#26313d',
    mcu_fill='url(#mcuGrad)', side_fill='#161b22', side_stroke='#30363d',
    text='#e6edf3', muted='#96a3b3', blue='#5b9bf0', blue_soft='#16273d',
    teal='#3fb9a8', amber='#d9a343', purple='#a98bf0', green='#57b871',
    arrow='#6e7f92',
    p=['#17263c', '#33290d', '#12301c', '#1c2128', '#241f38'],
    pb=['#5b9bf0', '#d9a343', '#57b871', '#8ba0b8', '#a98bf0'],
)

TXT = {
 'zh': dict(
   title='系统架构与数据链路',
   sub='上位机经 RS485 下发命令与固件；三条采集链路收敛到同一个协议出口',
   tag='GD32F470VE · App + BootLoader 双工程',
   host='上位机', host1='RS485 主站：ASCII-Hex 命令帧', host2='固件下发与安装流程校验',
   bus='RS485',
   mcu='GD32F470VE　Cortex-M4 · 512 KB Flash / 256 KB SRAM',
   flash='Flash 分区（0x08000000 起）',
   part=['BootLoader\n64 KB', '参数\n4 KB', 'APP1\n128 KB', '备份\n128 KB', '下载暂存\n128 KB'],
   bl_t='BootLoader（0x08000000，64 KB）',
   bl=['启动决策：参数主备与完整性校验', '官方固件流接收（USART1 / RS485）',
       '备份 → 安装 → CRC32 复核', '失败回滚 · APP 向量表校验', '释放串口资源后 SCB->VTOR 跳转'],
   bl_note='（APP 与 BootLoader 分区相邻性带编译期断言）',
   app_t='App（0x08011000，128 KB）',
   app_sub='协作式时间片轮询，无 RTOS',
   layers=[('User', '启动与中断向量'), ('Function', '业务服务'), ('Protocol', '帧编解码 / CRC'),
           ('Driver', '外设驱动')],
   common_name='common/', common_desc='Flash 分区与参数布局，两工程共享',
   per_t='片上外设与服务',
   per=['ADC　CH0 / CH1', 'DAC　CH1 回读', 'GD30AD3344　SPI + DMA', 'RS485　USART1 + DMA',
        'InternalFlash / SPI Flash', 'OLED · RTC · TIMER', 'KEY · LED · 睡眠唤醒'],
   per_note='三条链路统一出口 →',
   hw_t='现场板卡（自绘）',
   b1_t='系统供电电源板',
   b1=['18–36 V → 5 V', 'P-MOS 防反接 · GD30DC1354', '15 µH · 输入 TVS · 多级滤波'],
   b2_t='PT100 温度采样板',
   b2=['约 1 mA 恒流激励', 'INA333 放大 10 倍 · RC 滤波', '2.048 V 基准 · AD3344（SPI）'],
   b3_t='精密电阻模拟测试板',
   b3=['12 档 0.1% 金属膜电阻', '−49.27 ℃ … 141.11 ℃', '经三线 I+/I−/COMP 接入采样板'],
   lab5v='5 V 供电', labspi='SPI · 调理电压',
   ch_t='三通道数据链',
   ch=[('CH0 电位器', '内部 12 位 ADC'), ('CH1 DAC 回读', '内部 12 位 ADC'),
       ('CH2 PT100', 'GD30AD3344 · SPI/DMA')],
   eg_t='统一协议出口',
   eg=['单位 · 变比 · IEEE754 大端', '单次查询与自动上报共用同一份换算'],
   out1='单次查询应答', out2='定时自动上报（CH0 / CH1）',
   foot='完整分区表、29 条命令表与两条升级路径状态机见 docs/architecture.md · docs/protocol.md · docs/ota.md',
 ),
 'en': dict(
   title='System Architecture and Data Path',
   sub='The host sends commands and firmware over RS485; all three acquisition chains converge on one protocol egress',
   tag='GD32F470VE · App + BootLoader',
   host='Host', host1='RS485 master: ASCII-Hex command frames', host2='Firmware delivery and install flow',
   bus='RS485',
   mcu='GD32F470VE　Cortex-M4 · 512 KB flash / 256 KB SRAM',
   flash='Flash partitions (from 0x08000000)',
   part=['BootLoader\n64 KB', 'Params\n4 KB', 'APP1\n128 KB', 'Backup\n128 KB', 'Download\n128 KB'],
   bl_t='BootLoader (0x08000000, 64 KB)',
   bl=['Boot decision: parameter copy + CRC check', 'Official firmware stream (USART1 / RS485)',
       'Backup → install → CRC32 re-check', 'Rollback on failure · APP vector check', 'Release UART, then SCB->VTOR jump'],
   bl_note='(partition contiguity is checked at compile time)',
   app_t='App (0x08011000, 128 KB)',
   app_sub='Cooperative time-sliced polling, no RTOS',
   layers=[('User', 'startup, IRQ vectors'), ('Function', 'business services'),
           ('Protocol', 'framing / CRC'), ('Driver', 'peripheral drivers')],
   common_name='common/', common_desc='flash partitions and parameter layout, shared by both projects',
   per_t='On-chip peripherals and services',
   per=['ADC　CH0 / CH1', 'DAC　CH1 readback', 'GD30AD3344　SPI + DMA', 'RS485　USART1 + DMA',
        'Internal flash / SPI flash', 'OLED · RTC · TIMER', 'KEY · LED · sleep / wake'],
   per_note='three chains → one egress',
   hw_t='Field boards (self-designed)',
   b1_t='System power supply',
   b1=['18–36 V → 5 V', 'P-MOS reverse protection · GD30DC1354', '15 µH · input TVS · multi-stage filtering'],
   b2_t='PT100 temperature front end',
   b2=['≈1 mA excitation current', 'INA333 gain ×10 · RC filtering', '2.048 V reference · AD3344 (SPI)'],
   b3_t='Precision resistor simulator',
   b3=['12 × 0.1 % metal-film resistors', '−49.27 °C … 141.11 °C', 'Wired to the front end via I+/I−/COMP'],
   lab5v='5 V supply', labspi='SPI · conditioned signal',
   ch_t='Three acquisition chains',
   ch=[('CH0 potentiometer', 'internal 12-bit ADC'), ('CH1 DAC readback', 'internal 12-bit ADC'),
       ('CH2 PT100', 'GD30AD3344 · SPI/DMA')],
   eg_t='Unified protocol egress',
   eg=['units · scale factors · big-endian IEEE754', 'one conversion shared by query and auto report'],
   out1='On-demand query response', out2='Periodic auto report (CH0 / CH1)',
   foot='Full partition table, the 29-command table and both upgrade state machines: docs/architecture.md · docs/protocol.md · docs/ota.md',
 ),
}

esc = lambda s: s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


def build(lang, dark):
    c = DARK if dark else LIGHT
    t = TXT[lang]
    o = []
    add = o.append

    add('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d" '
        'role="img" aria-label="%s" font-family="%s">' % (W, H, W, H, esc(t['title']), FONT))
    add('<defs>')
    add('<linearGradient id="mcuGrad" x1="0" y1="0" x2="0" y2="1">'
        '<stop stop-color="%s"/><stop offset="1" stop-color="%s"/></linearGradient>'
        % (('#f9fcff', '#eef5ff') if not dark else ('#171d26', '#11161e')))
    add('<pattern id="grid" width="26" height="26" patternUnits="userSpaceOnUse">'
        '<circle cx="1.2" cy="1.2" r="0.9" fill="%s" opacity="%s"/></pattern>'
        % (c['grid'], '0.5' if not dark else '0.55'))
    for key in ('arrow', 'blue', 'teal', 'purple'):
        add('<marker id="ar-%s" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" '
            'orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="%s"/></marker>'
            % (key, c[key]))
    add('</defs>')

    add('<rect x="0.5" y="0.5" width="%d" height="%d" rx="18" fill="%s" stroke="%s"/>'
        % (W - 1, H - 1, c['page'], c['frame']))
    add('<rect x="0.5" y="0.5" width="%d" height="%d" rx="18" fill="url(#grid)" opacity="0.35"/>'
        % (W - 1, H - 1))

    def box(x, y, w, h, fill, stroke, rx=12, sw=1.4):
        add('<rect x="%s" y="%s" width="%s" height="%s" rx="%s" fill="%s" stroke="%s" stroke-width="%s"/>'
            % (x, y, w, h, rx, fill, stroke, sw))

    def text(x, y, s, size=12, fill=None, weight='normal', anchor='start'):
        add('<text x="%s" y="%s" font-size="%s" fill="%s" text-anchor="%s" font-weight="%s">%s</text>'
            % (x, y, size, fill or c['text'], anchor, weight, esc(s)))

    def line(x1, y1, x2, y2, color=None, marker=None, sw=1.6):
        add('<line x1="%s" y1="%s" x2="%s" y2="%s" stroke="%s" stroke-width="%s"%s/>'
            % (x1, y1, x2, y2, color or c['arrow'], sw,
               ' marker-end="url(#ar-%s)"' % marker if marker else ''))

    def path(d, color=None, marker=None, dashed=False, sw=1.6):
        add('<path d="%s" fill="none" stroke="%s" stroke-width="%s"%s%s/>'
            % (d, color or c['arrow'], sw, ' stroke-dasharray="6 5"' if dashed else '',
               ' marker-end="url(#ar-%s)"' % marker if marker else ''))

    # 标题
    text(48, 48, t['title'], 23, weight='600')
    text(48, 74, t['sub'], 13, c['muted'])
    text(W - 48, 48, t['tag'], 12.5, c['muted'], anchor='end')
    add('<line x1="48" y1="92" x2="%d" y2="92" stroke="%s"/>' % (W - 48, c['frame']))

    # 上位机 + RS485
    box(48, 112, 292, 112, c['side_fill'], c['side_stroke'])
    text(68, 144, t['host'], 16, weight='600')
    text(68, 172, t['host1'], 12, c['muted'])
    text(68, 194, t['host2'], 12, c['muted'])
    line(344, 168, 396, 168, marker='blue')
    line(396, 168, 344, 168, marker='blue')
    text(370, 158, t['bus'], 10.5, c['blue'], anchor='middle')

    # MCU 面板
    box(400, 112, W - 448, 468, c['mcu_fill'], c['side_stroke'], rx=14)
    text(424, 146, t['mcu'], 15, weight='600')

    # Flash 分区条
    text(424, 176, t['flash'], 11, c['muted'])
    seg_w = [98, 62, 194, 194, 196]
    x = 424
    for i, (w, lab) in enumerate(zip(seg_w, t['part'])):
        box(x, 186, w, 44, c['p'][i], c['pb'][i], rx=7, sw=1.2)
        a, b = lab.split('\n')
        text(x + w / 2, 205, a, 10.5, c['text'], weight='600', anchor='middle')
        text(x + w / 2, 220, b, 9.5, c['muted'], anchor='middle')
        x += w + 2

    # 三列：BootLoader / App 分层 / 片上外设
    box(424, 250, 276, 306, c['side_fill'], c['side_stroke'])
    text(444, 278, t['bl_t'], 12.5, weight='600')
    yy = 306
    for s in t['bl']:
        add('<circle cx="450" cy="%s" r="2.6" fill="%s"/>' % (yy - 4, c['blue']))
        text(462, yy, s, 11.5, c['text'])
        yy += 30
    text(444, 526, t['bl_note'], 10, c['muted'])

    box(716, 250, 244, 306, c['side_fill'], c['side_stroke'])
    text(736, 278, t['app_t'], 12.5, weight='600')
    text(736, 298, t['app_sub'], 10.5, c['muted'])
    ly = 314
    for name, desc in t['layers']:
        box(736, ly, 204, 38, c['blue_soft'], c['side_stroke'], rx=8, sw=1)
        text(750, ly + 24, name, 12.5, c['blue'], weight='600')
        text(930, ly + 24, desc, 10.5, c['muted'], anchor='end')
        ly += 46
    add('<rect x="736" y="496" width="204" height="26" rx="8" fill="none" stroke="%s" '
        'stroke-dasharray="5 4"/>' % c['side_stroke'])
    text(838, 513, t['common_name'], 10, c['muted'], anchor='middle')
    text(838, 542, t['common_desc'], 9.5, c['muted'], anchor='middle')

    box(976, 250, W - 1024, 306, c['side_fill'], c['side_stroke'])
    text(996, 278, t['per_t'], 12.5, weight='600')
    yy = 306
    for s in t['per']:
        text(996, yy, s, 11.5, c['text'])
        yy += 30
    add('<line x1="996" y1="%d" x2="%d" y2="%d" stroke="%s"/>' % (yy - 8, W - 68, yy - 8, c['side_stroke']))
    text(996, yy + 20, t['per_note'], 10.5, c['teal'])

    # 现场板卡
    text(48, 606, t['hw_t'], 14, weight='600')
    boards = [(48, c['amber'], t['b1_t'], t['b1']), (446, c['teal'], t['b2_t'], t['b2']),
              (844, c['purple'], t['b3_t'], t['b3'])]
    for bx, col, title, lines in boards:
        box(bx, 622, 348, 118, c['side_fill'], c['side_stroke'])
        add('<rect x="%s" y="622" width="4" height="118" rx="2" fill="%s"/>' % (bx, col))
        text(bx + 22, 652, title, 13.5, weight='600')
        yy = 678
        for s in lines:
            text(bx + 22, yy, s, 11.5, c['muted'])
            yy += 22

    # 板卡 → MCU 的连线
    path('M 222 622 L 222 598 L 500 598 L 500 582', color=c['amber'], marker='arrow', dashed=True)
    text(232, 592, t['lab5v'], 10.5, c['amber'])
    path('M 620 622 L 620 582', color=c['teal'], marker='arrow')
    text(630, 600, t['labspi'], 10.5, c['teal'])

    # 三通道数据链
    text(48, 786, t['ch_t'], 14, weight='600')
    for (cx, (name, desc)) in zip([48, 216, 384], t['ch']):
        box(cx, 800, 152, 74, c['side_fill'], c['side_stroke'])
        text(cx + 16, 828, name, 12.5, weight='600')
        text(cx + 16, 852, desc, 10.5, c['muted'])
        line(cx + 152, 837, 566, 837, color=c['arrow'], sw=1.3)
    line(566, 837, 596, 837, color=c['blue'], marker='blue')

    box(600, 800, 320, 74, c['blue_soft'], c['blue'], rx=12, sw=1.6)
    text(620, 828, t['eg_t'], 13.5, c['blue'], weight='600')
    text(620, 850, t['eg'][0], 10.5, c['text'])
    text(620, 866, t['eg'][1], 10.5, c['muted'])

    box(940, 800, 252, 34, c['side_fill'], c['side_stroke'], rx=8)
    text(956, 822, t['out1'], 11.5, c['text'])
    box(940, 840, 252, 34, c['side_fill'], c['side_stroke'], rx=8)
    text(956, 862, t['out2'], 11.5, c['text'])
    line(920, 817, 936, 817, color=c['blue'], marker='blue')
    line(920, 857, 936, 857, color=c['blue'], marker='blue')

    text(48, 918, t['foot'], 10.5, c['muted'])
    add('</svg>')
    return '\n'.join(o)


def main():
    os.makedirs(OUT, exist_ok=True)
    for lang in ('zh', 'en'):
        for dark in (False, True):
            name = 'architecture-%s-%s.svg' % (lang, 'dark' if dark else 'light')
            with open(os.path.join(OUT, name), 'w', encoding='utf-8', newline='\n') as f:
                f.write(build(lang, dark))
            print('written assets/%s' % name)


if __name__ == '__main__':
    main()
