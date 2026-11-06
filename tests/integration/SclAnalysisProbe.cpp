#include "SclReader.hpp"
#include "SclReferenceResolver.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include <iterator>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    const bool resolve = args.size() == 3 && args[2] == "--references";
    if (args.size() != 2 && !resolve) {
        std::fputs("Usage: SclAnalysisProbe <local-scl-file> [--references]\n", stderr);
        return 2;
    }

    QElapsedTimer timer;
    timer.start();
    const auto result = Scl::Reader::readFile(args[1]);
    const auto elapsed = timer.elapsed();
    if (!result.document) {
        std::fprintf(stderr, "SCL read failed (code %d, line %lld)\n", int(result.diagnostic.code),
                     static_cast<long long>(result.diagnostic.line));
        return 1;
    }

    QJsonObject counts;
    for (auto it = result.document->standardCounts.cbegin(); it != result.document->standardCounts.cend(); ++it)
        counts.insert(it.key(), it.value());

    QJsonObject metrics{
        {"fileBytes", double(result.source->bytes().size())},
        {"elapsedMs", double(elapsed)},
        {"lines", result.source->lineCount()},
        {"nodes", double(result.document->nodes.size())},
        {"attributes", double(result.document->attributes.size())},
        {"lastElementLine", result.document->nodes.back().source.line},
        {"standardCounts", counts}
    };
    if (resolve) {
        timer.restart();
        const auto analysis = Scl::ReferenceResolver::analyze(result);
        metrics.insert("referenceElapsedMs", double(timer.elapsed()));
        metrics.insert("sha256", QString::fromLatin1(analysis.sha256));
        metrics.insert("reports", double(analysis.reports.size()));
        constexpr const char *Kinds[] = {"logicalNodeType", "memberType", "controlDataSet", "fcda",
            "connectedAp", "networkControl", "extRefData", "extRefControl"};
        constexpr const char *States[] = {"resolved", "missing", "ambiguous", "unbound", "external",
            "unsupported", "blocked", "partial"};
        int totals[std::size(Kinds)][std::size(States)]{};
        for (const auto &ref : analysis.references)
            ++totals[int(ref.kind)][int(ref.state)];
        QJsonObject references;
        for (size_t kind = 0; kind < std::size(Kinds); ++kind) {
            QJsonObject states;
            for (size_t state = 0; state < std::size(States); ++state)
                states.insert(States[state], totals[kind][state]);
            references.insert(Kinds[kind], states);
        }
        metrics.insert("references", references);
        QJsonObject findings;
        for (const auto &finding : analysis.findings)
            findings.insert(finding.rule, findings.value(finding.rule).toInt() + 1);
        metrics.insert("findings", findings);
    }
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof counters))
        metrics.insert("peakWorkingSetBytes", double(counters.PeakWorkingSetSize));
#endif
    const auto output = QJsonDocument(metrics).toJson();
    std::fwrite(output.constData(), 1, output.size(), stdout);
}
