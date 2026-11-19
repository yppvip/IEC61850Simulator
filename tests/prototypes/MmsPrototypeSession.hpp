#pragma once

#include <QObject>
#include <QMap>
#include <QStringList>
#include <memory>

namespace iec61850sim::prototype {

enum class Page { Overview, Reports, Members, Signals, Attributes, Clients, Connections, Checks, Search, Advanced };
struct Cell {
    QString display;
    QString value;
    Cell() = default;
    explicit Cell(QString text) : display(text), value(std::move(text)) {}
    Cell(QString shown, QString raw) : display(std::move(shown)), value(std::move(raw)) {}
};
struct Field { QString name; Cell cell; QString origin; QString evidence; };
struct Record {
    QString id;
    Page page;
    QString ied;
    QString title;
    QString evidence;
    QList<Cell> cells;
    QList<Field> fields;
    QString report;
    QString target;
    Page targetPage = Page::Overview;
};
struct Snapshot {
    QString id;
    QList<Record> records;
    QStringList xml;
    QMap<QString, int> evidenceLines; // One-based, in this synthetic snapshot only.
    const Record* Find(const QString& id) const;
    static std::shared_ptr<const Snapshot> Demo(const QString& id = QStringLiteral("demo-1"));
};
struct Location {
    QString snapshot;
    Page page = Page::Overview;
    QString ied = QStringLiteral("DemoIED");
    QString selected;
    QString parent;
    QString filter;
    QString query;
    QString source;
    QString field;
    int column = 0;
    QString topId;
    int topOffset = 0;
};

// A bounded synthetic ViewModel. No file parser, protocol or real-file access.
class MmsPrototypeSession final : public QObject {
    Q_OBJECT
public:
    explicit MmsPrototypeSession(QObject* parent = nullptr);
    void SetSnapshot(std::shared_ptr<const Snapshot> snapshot);
    const Snapshot& Data() const { return *m_snapshot; }
    const Location& Current() const { return m_location; }
    QList<const Record*> Rows() const;
    const Record* Selected() const { return m_snapshot->Find(m_location.selected); }
    void Navigate(Page page, const QString& parent = {});
    void SetScope(const QString& ied);
    void SetFilter(const QString& text);
    void Select(const QString& id, int column = 0);
    void SetField(const QString& name);
    void SetAnchor(const QString& topId, int offset);
    void Drill();
    void ShowClients();
    void OpenSource(const QString& evidence = {});
    void CloseSource();
    void Search(const QString& text);
    void Back();
    void Forward();
    bool CanBack() const { return m_cursor > 0; }
    bool CanForward() const { return m_cursor + 1 < m_history.size(); }
    static QString PageName(Page page);
signals:
    void Changed(bool rowsChanged);
private:
    void Commit(Location location, bool rowsChanged);
    std::shared_ptr<const Snapshot> m_snapshot;
    Location m_location;
    QList<Location> m_history;
    int m_cursor = 0;
};
}
