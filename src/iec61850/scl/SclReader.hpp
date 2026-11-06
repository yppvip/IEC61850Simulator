#pragma once

#include "SclDocument.hpp"
#include "SclTaskControl.hpp"
#include <memory>

namespace Scl {

struct Limits {
    int fileBytes = 256 * 1024 * 1024;
    int depth = 256;
    int nodes = 2'000'000;
    int attributes = 6'000'000;
    int valueChars = 1024 * 1024;
    int lines = 8'000'000;
};

enum class Error { None, File, Empty, Xml, UnsupportedRoot, UnsupportedEncoding, Dtd, Limit };

struct Diagnostic {
    Error code = Error::None;
    QString detail;
    qint64 line = 0;   // One-based; zero means unavailable.
    qint64 column = 0; // Parser column, zero means unavailable.
};

struct Result {
    std::shared_ptr<const SourceIndex> source;
    std::shared_ptr<const Document> document;
    Diagnostic diagnostic;
};

class Reader {
public:
    static Result readFile(const QString &path, const Limits &limits = {}, TaskControl *task = nullptr);
    static Result readBytes(QByteArray bytes, const Limits &limits = {}, TaskControl *task = nullptr);
};

} // namespace Scl
