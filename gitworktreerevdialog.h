#ifndef GITWORKTREEREVDIALOG_H
#define GITWORKTREEREVDIALOG_H

#include <QDialog>
#include <QString>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

struct GitWorktreeQtOpenResult {
    bool opened = false;
    bool cancelled = false;
    QString proFile;
    QString error;
};

/** Open the top-level .pro in projectDir with Qt Creator. */
GitWorktreeQtOpenResult openWorktreeInQtCreator(QWidget *parent, const QString &projectDir);

class GitWorktreeRevDialog : public QDialog
{
    Q_OBJECT
public:
    explicit GitWorktreeRevDialog(const QString &mainRepoDir, QWidget *parent = nullptr);

    QString createdPath() const { return m_createdPath; }
    QString logText() const { return m_log; }
    bool createdOk() const { return m_createdOk; }

private slots:
    void onFilterChanged(const QString &text);
    void onSelectionChanged();
    void onCreateOnly();
    void onCreateAndOpen();

private:
    void loadCommits();
    void applySelectionToFields();
    bool createAtSelection(bool openQt);
    void setBusy(bool busy);

    QString m_mainRepoDir;
    QString m_createdPath;
    QString m_log;
    QString m_headHash;
    bool m_createdOk = false;
    bool m_pathEdited = false;

    QLabel *m_lblHint = nullptr;
    QLineEdit *m_txtFilter = nullptr;
    QTableWidget *m_table = nullptr;
    QLineEdit *m_txtRev = nullptr;
    QLineEdit *m_txtPath = nullptr;
    QPushButton *m_btnCreateOpen = nullptr;
    QPushButton *m_btnCreate = nullptr;
};

#endif // GITWORKTREEREVDIALOG_H
