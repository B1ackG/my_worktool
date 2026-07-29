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

## 对外汇报 / 日报润色

写日报、采集表功能描述、或给上级/客户看的文字时，遵循：

- Rule: [`.cursor/rules/external-report-writing/RULE.md`](.cursor/rules/external-report-writing/RULE.md)

「复制到日报」会经 DeepSeek 按 `MainWindow::dailyReportPolishSystemPrompt()` 润色；改准则时请同步改该函数与上述 rule。
