#pragma once

#include "MmsPrototypeSession.hpp"
#include <QMainWindow>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTableView;
class QTreeWidget;
namespace ads { class CDockManager; class CDockWidget; }

namespace iec61850sim::prototype {
class PrototypeTableModel;
class MmsPrototypeWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MmsPrototypeWindow(QWidget* parent = nullptr);
    ~MmsPrototypeWindow() override;
    MmsPrototypeSession* Session() const { return m_session; }
protected:
    void resizeEvent(QResizeEvent* event) override;
private:
    void Present(bool rowsChanged);
    void OpenFieldSource();
    void ResetLayout();
    void BalanceCompactPanels();
    MmsPrototypeSession* m_session;
    ads::CDockManager* m_docks = nullptr;
    ads::CDockWidget* m_navigationDock = nullptr;
    ads::CDockWidget* m_contentDock = nullptr;
    ads::CDockWidget* m_detailsDock;
    QTreeWidget* m_tasks;
    QComboBox* m_taskPicker;
    QComboBox* m_scope;
    QLineEdit* m_global;
    QLineEdit* m_filter;
    QLabel* m_breadcrumb;
    QLabel* m_summary;
    QLabel* m_detailTitle;
    QLabel* m_sourceTitle;
    QLabel* m_sourceLines;
    QPlainTextEdit* m_source;
    QTableView* m_table;
    QTableView* m_fields;
    PrototypeTableModel* m_rows;
    PrototypeTableModel* m_fieldRows;
    QStackedWidget* m_detailStack;
    QPushButton* m_back;
    QPushButton* m_forward;
    QPushButton* m_drill;
    QPushButton* m_clients;
    QPushButton* m_sourceAction;
    QPushButton* m_fieldSource;
    QCheckBox* m_technical;
    QCheckBox* m_details;
    QByteArray m_defaultLayout;
    bool m_applying = false;
    bool m_narrow = false;
};
}
