#ifndef QUICKERKEYBOARDUTILS_H
#define QUICKERKEYBOARDUTILS_H

#include <QComboBox>
#include <QKeyEvent>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

struct KeyboardKeyCategory
{
    QString label;
    QStringList keys;
};

QVector<KeyboardKeyCategory> keyboardKeyCategories();
QSet<QString> knownXdotoolKeyNames();
QComboBox *createCategorizedKeyboardCombo(QWidget *parent);
void populateCategorizedKeyboardCombo(QComboBox *combo);

QString joinStepParts(const QStringList &parts);
QStringList splitStepParts(const QString &step);
QString joinComboSteps(const QStringList &steps);
QStringList splitComboSteps(const QString &combo);
QString formatComboPreview(const QString &combo);

QString qtKeyEventToXdotoolStep(const QKeyEvent *event);
QStringList validateComboParts(const QString &combo);

bool isEmptyKeyboardPart(const QString &part);
QString keyboardPartText(const QComboBox *combo);
void setKeyboardPartCombo(QComboBox *combo, const QString &part);

#endif // QUICKERKEYBOARDUTILS_H
