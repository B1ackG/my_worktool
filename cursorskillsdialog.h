#ifndef CURSORSKILLSDIALOG_H
#define CURSORSKILLSDIALOG_H

#include <QDialog>
#include <QString>
#include <QVector>

class QLabel;
class QTableWidget;

struct CursorSkillRepoRef {
    QString displayName;
    QString repoPath;
};

class CursorSkillsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CursorSkillsDialog(const QVector<CursorSkillRepoRef> &repos,
                                QWidget *parent = nullptr);

    static QString globalSkillsRoot();

private slots:
    void onRefresh();
    void onOpenDir();
    void onOpenSkillFile();
    void onOpenSelected();

private:
    struct SkillRow {
        QString sourceLabel;
        QString skillName;
        QString skillDir;
        QString skillFile; // SKILL.md if present, else empty
        bool exists = false;
    };

    void rebuildTable();
    int selectedRow() const;
    SkillRow rowAt(int row) const;
    QVector<SkillRow> collectSkills() const;
    static void appendSkillsFromRoot(const QString &sourceLabel, const QString &skillsRoot,
                                     QVector<SkillRow> *out);

    QVector<CursorSkillRepoRef> m_repos;
    QTableWidget *m_table = nullptr;
    QLabel *m_lblHint = nullptr;
};

#endif // CURSORSKILLSDIALOG_H
