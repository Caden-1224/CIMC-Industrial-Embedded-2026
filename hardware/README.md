# hardware

本目录存放本项目自行设计的三块板卡的硬件资料：嘉立创 EDA 专业版工程归档，以及从中导出的原理图与 PCB 渲染图。

## 目录内容

```text
hardware/
├── eda/
│   └── CIMC-2026-boards.epro2     嘉立创 EDA 专业版工程归档（原理图 + PCB 源文件，可编辑）
└── images/                        六张导出的渲染图
    ├── power-supply-schematic.webp    系统供电电源 · 原理图
    ├── power-supply-pcb.webp          系统供电电源 · PCB
    ├── pt100-schematic.webp           PT100 温度采样板 · 原理图
    ├── pt100-pcb.webp                 PT100 温度采样板 · PCB
    ├── rtd-simulator-schematic.webp  精密电阻模拟测试板 · 原理图
    └── rtd-simulator-pcb.webp        精密电阻模拟测试板 · PCB
```

## 如何打开 EDA 工程

1. 安装嘉立创 EDA 专业版（桌面客户端）。
2. 菜单选择「文件 → 打开 → 嘉立创 EDA 专业版工程」，选择 `eda/CIMC-2026-boards.epro2`。
3. 打开后左侧工程树中可见 PCB、原理图与板卡文档；`.epro2` 为归档格式，导入后可正常编辑与导出。

注意：该归档为只读参考资料，请勿直接在其中修改；如需改动请另存副本。

## 图片使用

`images/` 中的六张渲染图按板卡成对组织，一一对应原理图与 PCB。文档中通过相对路径引用，例如从 `docs/` 下的文档引用时为 `../hardware/images/xxx.webp`，从本目录内的文档引用时为 `images/xxx.webp`。

PCB 图上印有团队编号 `2026397013`，为竞赛团队编号，非物料号。

## 相关文档

- [硬件设计](../docs/hardware.md)：三块板卡的完整设计说明（设计目标、器件选型、原理图要点、PCB 分区策略、接口定义、信号链核算、验证边界与待补事项）。
- [GD32F470 开发板引脚分配建议](../docs/hardware-pinout.md)：开发板资源划分与 `SPI3` 外接 ADC 的引脚规划。

## 声明

原理图与 PCB 均为本队自绘，随仓库供学习参考。文档中标注「待在实物/归档中复核」的内容尚未取得归档或实测确认，请以实物核对为准。
