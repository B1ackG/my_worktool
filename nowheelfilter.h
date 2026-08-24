#ifndef NOWHEELFILTER_H
#define NOWHEELFILTER_H

#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QLineEdit>
#include <QObject>
#include <QScrollArea>
#include <QWidget>

/** Block mouse-wheel from changing combo-box selection; forward wheel to a parent QScrollArea. */
class NoWheelFilter : public QObject
{
public:
    static void install(QWidget *widget)
    {
        if (!widget) {
            return;
        }
        widget->installEventFilter(instance());
        if (auto *combo = qobject_cast<QComboBox *>(widget)) {
            combo->setFocusPolicy(Qt::StrongFocus);
            if (QLineEdit *edit = combo->lineEdit()) {
                edit->installEventFilter(instance());
            }
        }
    }

    static void installOnComboBoxes(QWidget *root)
    {
        if (!root) {
            return;
        }
        if (auto *combo = qobject_cast<QComboBox *>(root)) {
            install(combo);
        }
        const auto combos = root->findChildren<QComboBox *>();
        for (QComboBox *combo : combos) {
            install(combo);
        }
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::Wheel) {
            return false;
        }

        auto *widget = qobject_cast<QWidget *>(watched);
        if (!widget) {
            return false;
        }

        for (QWidget *parent = widget->parentWidget(); parent; parent = parent->parentWidget()) {
            if (auto *scroll = qobject_cast<QScrollArea *>(parent)) {
                QCoreApplication::sendEvent(scroll->viewport(), event);
                return true;
            }
        }
        event->ignore();
        return true;
    }

private:
    NoWheelFilter() = default;

    static NoWheelFilter *instance()
    {
        static NoWheelFilter filter;
        return &filter;
    }
};

#endif
