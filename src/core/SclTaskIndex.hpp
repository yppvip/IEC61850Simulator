#pragma once

#include "SclSignalCatalog.hpp"
#include <QHash>
#include <map>

namespace iec61850sim
{
struct SclBrowseIndex;
struct SclSnapshot;

enum class SclTaskKind { Signal, Input, Goose, GooseInput, Sv, SvInput, Finding };

struct SclTaskRow
{
    SclTaskKind kind;
    int index;
};

struct SclTaskDetail
{
    struct Evidence { QString label; Scl::NodeId node; };
    QString text;
    std::vector<Evidence> evidence;
};

struct SclTaskIndex
{
    struct Usage
    {
        int reference;
        Scl::NodeId dataSet;
    };
    struct Stream
    {
        Scl::NodeId control;
        Scl::NodeId dataSet;
        std::vector<Scl::NodeId> endpoints;
        std::vector<Scl::NodeId> receivers;
    };
    struct Input
    {
        Scl::NodeId node;
        int dataReference = -1;
        int controlReference = -1;
        Scl::NodeId matchedStream = Scl::NoNode;
        bool inferred = false;
        bool ambiguousControl = false;
    };

    Scl::SignalCatalog catalog;
    QHash<Scl::NodeId, std::vector<int>> signalsByIed;
    std::map<std::pair<Scl::NodeId, QString>, std::vector<Usage>> usages;
    QHash<Scl::NodeId, std::vector<Scl::NodeId>> controlsByDataSet;
    std::vector<Stream> goose;
    std::vector<Stream> sv;
    std::vector<Input> inputs;
    std::map<std::pair<Scl::NodeId, QString>, std::vector<int>> inputsBySignal;
    bool relationsLimited = false;

    static SclTaskIndex Build(const Scl::ReferenceAnalysis& analysis, const SclBrowseIndex& browse,
                              Scl::TaskControl* task);
};

QString SclTaskPath(const Scl::Document& document, Scl::NodeId node);
QString SclSignalCategory(const Scl::SignalDefinition& definition);
QStringList SclTaskCells(const SclSnapshot& snapshot, SclTaskRow row);
std::vector<SclTaskRow> FilterSclTaskRows(const SclSnapshot& snapshot, SclTaskKind kind, Scl::NodeId ied,
                                         const QString& text, Scl::TaskControl* task);
SclTaskDetail DescribeSclTaskRow(const SclSnapshot& snapshot, SclTaskRow row, Scl::TaskControl* task);
}
