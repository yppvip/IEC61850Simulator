#pragma once

#include "SclAnalysisService.hpp"

namespace iec61850sim
{
struct SclConnection
{
    Scl::NodeId control = Scl::NoNode;
    Scl::NodeId sender = Scl::NoNode;
    Scl::NodeId receiver = Scl::NoNode;
    Scl::NodeId unresolvedInput = Scl::NoNode;
    QString service, sourceName, targetName, controlName, identifier, addresses;
    int bound = 0;
    int candidates = 0;
    std::vector<int> links;
};

struct SclConnectionList
{
    std::vector<SclConnection> rows;
    QString statistics;
    bool limited = false;
};

struct SclConnectionMember
{
    QStringList cells;
    Scl::NodeId member = Scl::NoNode;
    Scl::NodeId input = Scl::NoNode;
    bool candidate = false;
};

struct SclConnectionDetail
{
    QString title, description;
    std::vector<SclTaskDetail::Evidence> evidence;
    std::vector<SclConnectionMember> members;
    int total = 0;
    static constexpr int PageSize = 200;
};

SclConnectionList QuerySclConnections(const SclSnapshot& snapshot, Scl::NodeId scope,
    const QString& service, const QString& text, Scl::TaskControl* task);
SclConnectionDetail DescribeSclConnection(const SclSnapshot& snapshot, const SclConnection& connection,
    int offset, bool mappedOnly, Scl::TaskControl* task);
QString SclStreamAddresses(const SclSnapshot& snapshot, Scl::NodeId control);
}
