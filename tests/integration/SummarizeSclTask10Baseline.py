"""Summarize explicit local T10.0 JSON probes; never reads engineering files."""
import json
import math
import statistics
import sys
from pathlib import Path


def stats(values):
    values = sorted(values)
    if not values:
        return {"n": 0}
    return {"n": len(values), "min": values[0], "median": statistics.median(values),
            "p95": values[math.ceil(len(values) * .95) - 1], "max": values[-1]}


root = Path(sys.argv[1])
summary = {"version": 1, "percentile": "nearest-rank ceil(0.95*n), no interpolation", "samples": {}}
for label in ("Scd", "Icd"):
    files = sorted(root.glob(f"{label}-[0-9][0-9].json"))
    records = [json.loads(p.read_text(encoding="utf-8")) for p in files]
    if not records:
        raise SystemExit(f"Missing records: {label}")
    if len({r["sha256"] for r in records}) != 1:
        raise SystemExit(f"Source fingerprints differ: {label}")
    attributes = [a for r in records for c in r["cases"] for a in c["attributes"]]
    visible = [a for a in attributes if a["returnCurrentVisible"]]
    by_case = {}
    for r in records:
        for c in r["cases"]:
            row = by_case.setdefault(c["path"], {"addressQueryUs": [], "reportQueryUs": [],
                                                "pageAndDetailMs": [], "returnsNotVisible": 0})
            row["addressQueryUs"].append(c["addressQueryUs"])
            for a in c["attributes"]:
                row["reportQueryUs"].append(a["reportQueryUs"])
                row["pageAndDetailMs"].append(a["firstPageAndDetailMs"])
                row["returnsNotVisible"] += not a["returnCurrentVisible"]
    for case in by_case.values():
        for key in ("addressQueryUs", "reportQueryUs", "pageAndDetailMs"):
            case[key] = stats(case[key])
    summary["samples"][label] = {
        "rounds": len(records), "sha256": records[0]["sha256"], "qt": records[0]["qt"],
        "readyMs": stats([r["readyMs"] for r in records]),
        "peakWorkingSetBytes": stats([r["peakWorkingSet"] for r in records]),
        "detailSelectionUs": stats([t for r in records for t in r["detailUs"]]),
        "returnIdentityUs": stats([t for r in records for t in r["returnUs"]]),
        "returnVisibleUsSuccessfulOnly": stats([a["returnVisibilityUs"] for a in visible]),
        "returnChecks": len(attributes), "returnsNotVisible": len(attributes) - len(visible),
        "allReturnsVisible": len(visible) == len(attributes), "cases": by_case}
    if all("structureIndexUs" in record for record in records):
        summary["samples"][label]["structureIndexUs"] = stats([r["structureIndexUs"] for r in records])
        summary["samples"][label]["structureIndexRecords"] = stats([r["structureIndexRecords"] for r in records])
        summary["samples"][label]["structureIndexLimited"] = any(r["structureIndexLimited"] for r in records)
target = root / "Summary.json"
target.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(json.dumps(summary, ensure_ascii=True, indent=2))
