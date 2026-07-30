# Agent guidance

## Qt C++ code review

When reviewing, auditing, or checking Qt C++ changes in this project, use the **qt-cpp-review** skill:

- Skill: [`.cursor/skills/qt-cpp-review/SKILL.md`](.cursor/skills/qt-cpp-review/SKILL.md)
- Cursor rule: [`.cursor/rules/qt-cpp-review/RULE.md`](.cursor/rules/qt-cpp-review/RULE.md)

Run the linter before deep analysis:

```bash
python3 .cursor/skills/qt-cpp-review/references/lint-scripts/qt_review_lint.py <files...>
```

This project uses **qmake** ([`ModbusTCPAssistant.pro`](ModbusTCPAssistant.pro)), not CMake. Do not apply `qt-project` CMake guidance unless migrating the build system.

## 对外汇报 / 提交说明表述

写日报、采集表功能描述、或给上级/客户看的文字时，遵循：

- Rule: [`.cursor/rules/external-report-writing/RULE.md`](.cursor/rules/external-report-writing/RULE.md)

「AI 整理提交说明」会经 DeepSeek 按 `MainWindow::commitMsgSystemPrompt()` 生成（含对外汇报表述准则）；「复制到日报」直接拼接当日提交说明，不再二次润色。改准则时请同步改该函数与上述 rule。
