#pragma once

#include "SclSourceIndex.hpp"
#include <QHash>
#include <QString>

namespace Scl {

inline const QString NamespaceUri = QStringLiteral("http://www.iec.ch/61850/2003/SCL");
using NodeId = int; // Scoped to its owning document, in XML order.
constexpr NodeId NoNode = -1;

struct Attribute {
    QString name;
    QString namespaceUri;
    QString value;
};

struct Node {
    QString name;
    QString namespaceUri;
    QString text; // Direct character data, without whitespace-only container indentation.
    SourceLocation source;
    NodeId parent = NoNode;
    NodeId firstChild = NoNode;
    NodeId nextSibling = NoNode;
    int firstAttribute = 0;
    int attributeCount = 0;
    bool isScl() const { return namespaceUri == NamespaceUri; }
};

struct Document {
    QString xmlEncoding; // XML declaration, possibly empty.
    std::vector<Node> nodes;
    std::vector<Attribute> attributes;
    QHash<QString, int> standardCounts; // Counts by local name within NamespaceUri.

    // Null means absent; a present empty attribute returns a non-null empty string.
    QString attribute(NodeId node, const QString &name, const QString &namespaceUri = {}) const
    {
        const auto &element = nodes.at(node);
        for (int i = element.firstAttribute; i < element.firstAttribute + element.attributeCount; ++i) {
            const auto &attr = attributes[i];
            if (attr.name == name && attr.namespaceUri == namespaceUri)
                return attr.value;
        }
        return {};
    }
};

} // namespace Scl
