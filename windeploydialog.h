#ifndef WINDEPLOYDIALOG_H
#define WINDEPLOYDIALOG_H

#include <QDialog>
#include <QString>
#include <QVector>

class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class WinDeployPackager;

struct WinDeployRepoRef {
    QString displayName;
    QString repoPath;
};

class WinDeployDialog : public QDialog
{
    Q_OBJECT
public:
    explicit WinDeployDialog(const QVector<WinDeployRepoRef> &repos, const QString &currentRepo,
                             QWidget *parent = nullptr);

private slots:
    void onRepoChanged();
    void onBrowseExe();
    void onBrowseOutput();
    void onBrowseQt();
    void onPackClicked();
    void onOpenOutputClicked();

private:
    QString currentRepoPath() const;
    void fillExeForRepo(const QString &repoDir);
    void loadSavedOutput(const QString &repoDir);
    void saveSettings() const;
    void appendLog(const QString &line);
    void setBusy(bool busy);

    QVector<WinDeployRepoRef> m_repos;
    WinDeployPackager *m_packager = nullptr;

    QComboBox *m_cmbRepo = nullptr;
    QLineEdit *m_editExe = nullptr;
    QLineEdit *m_editOutput = nullptr;
    QLineEdit *m_editQt = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QPushButton *m_btnPack = nullptr;
    QPushButton *m_btnOpen = nullptr;
};

#endif // WINDEPLOYDIALOG_H
