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
