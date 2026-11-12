#pragma once

#include <QWidget>

class QAbstractItemModel;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTableView;
class QTabBar;
class QTextBrowser;

namespace iec61850sim
{
class SclTaskPages final : public QWidget
{
    Q_OBJECT
public:
    enum Page { Overview, Signals, Goose, SampledValues, Checks, PageCount };
    explicit SclTaskPages(QWidget* parent = nullptr);
    void SetScopeOptions(const QList<QPair<int, QString>>& options, int current);
    void SetScope(int id);
    void SetPage(Page page);
    void SetModel(QAbstractItemModel* model);
    void SetOverview(const QString& text);
    void SetStatus(const QString& text);
    void SetDetail(const QString& text, bool hasSource);
    void SetEvidence(const QStringList& labels);
    int EvidenceIndex() const;
    void SetFilterText(const QString& text);
    void SetInputDirection(bool inputs);
    void SetCoverage(const QString& summary, const QString& rules);
    QTableView* Table() const { return m_table; }

signals:
    void ScopeChanged(int id);
    void PageChanged(Page page);
    void FilterChanged(const QString& text);
    void DirectionChanged(bool inputs);
    void Selected(const QModelIndex& row);
    void OpenSource();
    void ExportIssues();

private:
    QComboBox* m_scope;
    int m_appliedScope = -1;
    QTabBar* m_pages;
    QStackedWidget* m_body;
    QLabel* m_overview;
    QLabel* m_status;
    QLineEdit* m_filter;
    QComboBox* m_direction;
    QTableView* m_table;
    QTextBrowser* m_detail;
    QPushButton* m_source;
    QComboBox* m_evidence;
    QWidget* m_checks;
    QLabel* m_coverage;
    QPushButton* m_coverageButton;
    QPushButton* m_exportIssues;
    QString m_rules;
};
}
