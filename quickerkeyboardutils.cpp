#include "quickerkeyboardutils.h"

#include <QCompleter>
#include <QSet>

namespace {

QString qtKeyToXdotoolName(int key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return QString(QChar(static_cast<ushort>(QLatin1Char('a').unicode() + (key - Qt::Key_A))));
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return QString(QChar(static_cast<ushort>(QLatin1Char('0').unicode() + (key - Qt::Key_0))));
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F35) {
        return QStringLiteral("F%1").arg(key - Qt::Key_F1 + 1);
    }

    switch (key) {
    case Qt::Key_Left:
        return QStringLiteral("Left");
    case Qt::Key_Right:
        return QStringLiteral("Right");
    case Qt::Key_Up:
        return QStringLiteral("Up");
    case Qt::Key_Down:
        return QStringLiteral("Down");
    case Qt::Key_Space:
        return QStringLiteral("space");
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return QStringLiteral("Return");
    case Qt::Key_Tab:
        return QStringLiteral("Tab");
    case Qt::Key_Escape:
        return QStringLiteral("Escape");
    case Qt::Key_Backspace:
        return QStringLiteral("BackSpace");
    case Qt::Key_Delete:
        return QStringLiteral("Delete");
    case Qt::Key_Insert:
        return QStringLiteral("Insert");
    case Qt::Key_Home:
        return QStringLiteral("Home");
    case Qt::Key_End:
        return QStringLiteral("End");
    case Qt::Key_PageUp:
        return QStringLiteral("Page_Up");
    case Qt::Key_PageDown:
        return QStringLiteral("Page_Down");
    case Qt::Key_Minus:
        return QStringLiteral("minus");
    case Qt::Key_Equal:
        return QStringLiteral("equal");
    case Qt::Key_Comma:
        return QStringLiteral("comma");
    case Qt::Key_Period:
        return QStringLiteral("period");
    case Qt::Key_Semicolon:
        return QStringLiteral("semicolon");
    case Qt::Key_Apostrophe:
        return QStringLiteral("apostrophe");
    case Qt::Key_BracketLeft:
        return QStringLiteral("bracketleft");
    case Qt::Key_BracketRight:
        return QStringLiteral("bracketright");
    case Qt::Key_Backslash:
        return QStringLiteral("backslash");
    case Qt::Key_Slash:
        return QStringLiteral("slash");
    case Qt::Key_QuoteLeft:
        return QStringLiteral("grave");
    case Qt::Key_VolumeMute:
        return QStringLiteral("XF86AudioMute");
    case Qt::Key_VolumeDown:
        return QStringLiteral("XF86AudioLowerVolume");
    case Qt::Key_VolumeUp:
        return QStringLiteral("XF86AudioRaiseVolume");
    case Qt::Key_MediaNext:
        return QStringLiteral("XF86AudioNext");
    case Qt::Key_MediaPrevious:
        return QStringLiteral("XF86AudioPrev");
    default:
        break;
    }
    return QString();
}

bool isModifierOnlyKey(int key)
{
    return key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt
           || key == Qt::Key_Meta || key == Qt::Key_AltGr;
}

} // namespace

QVector<KeyboardKeyCategory> keyboardKeyCategories()
{
    QStringList letters;
    for (QChar c = QLatin1Char('a'); c <= QLatin1Char('z'); c = QChar(c.unicode() + 1)) {
        letters << QString(c);
    }

    QStringList digits;
    for (QChar c = QLatin1Char('0'); c <= QLatin1Char('9'); c = QChar(c.unicode() + 1)) {
        digits << QString(c);
    }

    QStringList functionKeys;
    for (int i = 1; i <= 12; ++i) {
        functionKeys << QStringLiteral("F%1").arg(i);
    }

    return {
        {QStringLiteral("修饰键"),
         {QStringLiteral("ctrl"), QStringLiteral("alt"), QStringLiteral("shift"),
          QStringLiteral("super"), QStringLiteral("meta")}},
        {QStringLiteral("字母"), letters},
        {QStringLiteral("数字"), digits},
        {QStringLiteral("方向/编辑"),
         {QStringLiteral("Left"), QStringLiteral("Right"), QStringLiteral("Up"),
          QStringLiteral("Down"), QStringLiteral("space"), QStringLiteral("Return"),
          QStringLiteral("Tab"), QStringLiteral("Escape"), QStringLiteral("BackSpace"),
          QStringLiteral("Delete"), QStringLiteral("Insert"), QStringLiteral("Home"),
          QStringLiteral("End"), QStringLiteral("Page_Up"), QStringLiteral("Page_Down")}},
        {QStringLiteral("功能键"), functionKeys},
        {QStringLiteral("媒体键"),
         {QStringLiteral("XF86AudioRaiseVolume"), QStringLiteral("XF86AudioLowerVolume"),
          QStringLiteral("XF86AudioMute"), QStringLiteral("XF86AudioNext"),
          QStringLiteral("XF86AudioPrev")}},
    };
}

QSet<QString> knownXdotoolKeyNames()
{
    QSet<QString> names;
    for (const KeyboardKeyCategory &category : keyboardKeyCategories()) {
        for (const QString &key : category.keys) {
            names.insert(key);
        }
    }
    return names;
}

void populateCategorizedKeyboardCombo(QComboBox *combo)
{
    if (!combo || combo->count() > 0) {
        return;
    }

    combo->setEditable(true);
    combo->setInsertPolicy(QComboBox::NoInsert);
    combo->setMaxVisibleItems(20);
    combo->addItem(QStringLiteral("（空）"), QString());

    for (const KeyboardKeyCategory &category : keyboardKeyCategories()) {
        combo->insertSeparator(combo->count());
        for (const QString &key : category.keys) {
            combo->addItem(key, key);
        }
    }

    auto *completer = new QCompleter(combo->model(), combo);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    combo->setCompleter(completer);
}

QComboBox *createCategorizedKeyboardCombo(QWidget *parent)
{
    auto *combo = new QComboBox(parent);
    populateCategorizedKeyboardCombo(combo);
    return combo;
}

bool isEmptyKeyboardPart(const QString &part)
{
    return part.isEmpty() || part == QStringLiteral("（空）");
}

QString keyboardPartText(const QComboBox *combo)
{
    if (!combo) {
        return QString();
    }
    const QString text = combo->currentText().trimmed();
    if (isEmptyKeyboardPart(text)) {
        return QString();
    }
    return text;
}

void setKeyboardPartCombo(QComboBox *combo, const QString &part)
{
    if (!combo) {
        return;
    }
    if (part.isEmpty()) {
        combo->setCurrentIndex(0);
        return;
    }

    const int index = combo->findText(part);
    if (index >= 0) {
        combo->setCurrentIndex(index);
        return;
    }

    combo->setEditText(part);
}

QString joinStepParts(const QStringList &parts)
{
    QStringList filtered;
    for (const QString &part : parts) {
        const QString trimmed = part.trimmed();
        if (!isEmptyKeyboardPart(trimmed)) {
            filtered.append(trimmed);
        }
    }
    return filtered.join(QLatin1Char('+'));
}

QStringList splitStepParts(const QString &step)
{
    const QStringList parts = step.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    QString part1;
    QString part2;
    QString part3;
    if (parts.size() >= 1) {
        part1 = parts.at(0);
    }
    if (parts.size() == 2) {
        part2 = parts.at(1);
    } else if (parts.size() == 3) {
        part2 = parts.at(1);
        part3 = parts.at(2);
    } else if (parts.size() > 3) {
        part2 = parts.at(1);
        part3 = parts.mid(2).join(QLatin1Char('+'));
    }
    return {part1, part2, part3};
}

QString joinComboSteps(const QStringList &steps)
{
    QStringList filtered;
    for (const QString &step : steps) {
        const QString trimmed = step.trimmed();
        if (!trimmed.isEmpty()) {
            filtered.append(trimmed);
        }
    }
    return filtered.join(QLatin1Char(' '));
}

QStringList splitComboSteps(const QString &combo)
{
    const QString trimmed = combo.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    return trimmed.split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

QString formatComboPreview(const QString &combo)
{
    const QStringList steps = splitComboSteps(combo);
    if (steps.isEmpty()) {
        return QString();
    }
    return steps.join(QStringLiteral(" → "));
}

QString qtKeyEventToXdotoolStep(const QKeyEvent *event)
{
    if (!event) {
        return QString();
    }

    const int key = event->key();
    if (key == Qt::Key_Escape) {
        return QString();
    }
    if (isModifierOnlyKey(key)) {
        return QString();
    }

    const QString keyName = qtKeyToXdotoolName(key);
    if (keyName.isEmpty()) {
        return QString();
    }

    QStringList parts;
    const Qt::KeyboardModifiers mods = event->modifiers();
    if (mods & Qt::ControlModifier) {
        parts << QStringLiteral("ctrl");
    }
    if (mods & Qt::AltModifier) {
        parts << QStringLiteral("alt");
    }
    if (mods & Qt::ShiftModifier) {
        parts << QStringLiteral("shift");
    }
    if (mods & Qt::MetaModifier) {
        parts << QStringLiteral("super");
    }
    parts << keyName;
    return parts.join(QLatin1Char('+'));
}

QStringList validateComboParts(const QString &combo)
{
    const QSet<QString> known = knownXdotoolKeyNames();
    QStringList unknown;
    for (const QString &step : splitComboSteps(combo)) {
        for (const QString &part : step.split(QLatin1Char('+'), Qt::SkipEmptyParts)) {
            const QString trimmed = part.trimmed();
            if (trimmed.isEmpty() || known.contains(trimmed)) {
                continue;
            }
            if (!unknown.contains(trimmed)) {
                unknown.append(trimmed);
            }
        }
    }
    return unknown;
}
