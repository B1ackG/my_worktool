#ifndef PLATFORMPREFERENCE_H
#define PLATFORMPREFERENCE_H

#include <QObject>
#include <QString>

class PlatformPreference : public QObject
{
    Q_OBJECT

public:
    enum Mode {
        Linux = 0,
        Windows = 1
    };
    Q_ENUM(Mode)

    static PlatformPreference &instance();

    Mode mode() const;
    void setMode(Mode mode);

    bool isLinux() const { return mode() == Linux; }
    bool isWindows() const { return mode() == Windows; }

    /** Preference is Linux and this binary was built for Linux. */
    bool linuxFeaturesAvailable() const;
    /** Preference is Windows and this binary was built for Windows. */
    bool windowsFeaturesAvailable() const;

    static Mode defaultMode();
    static QString modeDisplayName(Mode mode);
    static QString settingsKey();

signals:
    void modeChanged(Mode mode);

private:
    explicit PlatformPreference(QObject *parent = nullptr);
    Mode m_mode;
};

#endif // PLATFORMPREFERENCE_H
