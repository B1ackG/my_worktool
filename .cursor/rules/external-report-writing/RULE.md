---
description: >-
  对外汇报与日报润色表述准则：去掉无业务含义的内部实现细节
  （随意寄存器号、自取名配置文件、函数类名等），用业务语言写成果。
  在「复制到日报」、软件信息采集表、验收文档等场景适用。
globs:
alwaysApply: false
---

# 对外汇报表述准则

写日报、采集表、设计/使用说明中的「主要功能 / 技术特点」、或任何给上级/客户看的文字时，遵守本准则。

## 必须去掉或改写

- 无业务含义的**寄存器号**（如 8192）——只写「多示教终端写权限互锁」等作用
- **自取名的配置文件**（如 `config.ini`、`feature_switches.ini`）——改写为「外部配置」
- 内部**函数名 / 类名 / 变量名 / 私有路径 / 调试开关名 / 提交哈希 / 分支细节**
- 过细实现黑话：影子寄存器、singleShot、CDAB 字序、具体功能码编号等（除非文档专章讲协议）

## 推荐写法

| 过细 | 对外 |
|------|------|
| 写 8192 寄存器 | 多示教终端写权限互锁 |
| config.ini / feature_switches.ini | 外部配置 / 功能开关配置 |
| 影子寄存器读改写 | 按位控制合并后整字下发 |
| QTimer::singleShot 分阶段写 | 按控制器扫描节奏分步下发 |
| libmodbus_backend.so | 通信协议处理可与主程序解耦升级 |
| Modbus TCP（非协议专章时） | 工业通信 / 工业以太网通信 |

## DeepSeek「复制到日报」

`MainWindow::onGitCopyForDailyReportClicked` 会调用 DeepSeek，system prompt 为
`dailyReportPolishSystemPrompt()`，内容与本准则一致。改准则时请同步改该函数。
