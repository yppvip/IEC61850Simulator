#pragma once

#include "SclReferenceResolver.hpp"

namespace iec61850sim
{
enum class SclDescriptionSource
{
    InstanceDU, InstanceObject, TemplateObject, ObjectType, InstanceAttribute, TemplateAttribute, Member
};

enum class SclDescriptionState { Missing, Empty, Present, Unresolved, Ambiguous, Array, SettingGroup };

struct SclDescriptionField
{
    SclDescriptionSource source;
    SclDescriptionState state = SclDescriptionState::Missing;
    Scl::NodeId node = Scl::NoNode;
    QString value;
    QList<Scl::NodeId> candidates;
};

struct SclDescription
{
    static constexpr auto RuleVersion = "1";
    Scl::NodeId logicalNode = Scl::NoNode;
    Scl::NodeId member = Scl::NoNode;
    Scl::NodeId objectInstance = Scl::NoNode;
    QString objectPath, attributePath, fc;
    int memberOrdinal = 0;
    int arrayIndex = -1;
    QList<SclDescriptionField> fields;

    const SclDescriptionField* ObjectTitle() const;
};

SclDescription DescribeSclSignal(const Scl::ReferenceAnalysis& analysis, Scl::NodeId ln,
    const QString& objectPath, Scl::TaskControl* task, Scl::NodeId array = Scl::NoNode, int index = -1);
SclDescription DescribeSclMember(const Scl::ReferenceAnalysis& analysis, Scl::NodeId member,
    Scl::TaskControl* task);
SclDescription DescribeSclAttribute(const Scl::ReferenceAnalysis& analysis, const SclDescription& object,
    const QString& path, const QString& fc, const QList<Scl::NodeId>& declarations, Scl::TaskControl* task,
    Scl::NodeId array = Scl::NoNode, int index = -1);
QString SclDescriptionLabel(SclDescriptionSource source);
QString SclDescriptionText(const SclDescriptionField& field);
}
