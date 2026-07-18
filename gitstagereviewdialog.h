#ifndef GITSTAGEREVIEWDIALOG_H
#define GITSTAGEREVIEWDIALOG_H

#include "gitstageguard.h"

#include <QDialog>
#include <QStringList>
#include <QVector>

class QCheckBox;
class QLabel;
class QTableWidget;

class GitStageReviewDialog : public QDialog
{
    Q_OBJECT
public:
    explicit GitStageReviewDialog(const QString &repoDir, const QVector<GitStageEntry> &entries,
                                  QWidget *parent = nullptr);

    QStringList selectedPaths() const { return m_selectedPaths; }
    QStringList blockedTrackedPaths() const { return m_blockedTrackedPaths; }
    bool appendIgnoreRequested() const { return m_appendIgnore; }
    bool uncacheBlockedRequested() const { return m_uncacheBlocked; }
    QStringList ignorePatternsToAppend() const { return m_ignorePatterns; }

private slots:
    void onAcceptClicked();
    void onSelectNormalOnly();
    void onSelectAllSafe();
    void onSelectNone();

private:
    void rebuildSummary();
    QStringList pathsForIgnoreSuggestion() const;

    QString m_repoDir;
    QVector<GitStageEntry> m_entries;
    QTableWidget *m_table = nullptr;
    QLabel *m_lblSummary = nullptr;
    QLabel *m_lblAutoIgnore = nullptr;
    QCheckBox *m_chkUncacheBlocked = nullptr;

    QStringList m_selectedPaths;
    QStringList m_blockedTrackedPaths;
    QStringList m_ignorePatterns;
    bool m_appendIgnore = false;
    bool m_uncacheBlocked = false;
};

#endif // GITSTAGEREVIEWDIALOG_H
