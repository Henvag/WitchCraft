"""Estimate camera-stage delay from native_probe --sample-follow CSV (read-only).

Use a stationary camera and repeated walking/stopping on clear ground. Results
describe sampled engine positions, not input-to-screen or compositor latency.
"""
import bisect
import csv
import json
import math
import sys


def analyze(path):
    with open(path, newline="") as stream:
        rows = []
        for raw in csv.DictReader(stream):
            try:
                row = {key: float(value) for key, value in raw.items()}
                if all(math.isfinite(value) for value in row.values()):
                    rows.append(row)
            except (ValueError, TypeError):
                continue
    if len(rows) < 20:
        raise ValueError("Too few valid samples")
    times = [row["seconds"] for row in rows]

    def pose(time, prefix):
        index = bisect.bisect_right(times, time) - 1
        if index < 0 or index + 1 >= len(rows):
            return None
        a, b = rows[index:index + 2]
        duration = b["seconds"] - a["seconds"]
        if duration <= 0 or duration > .05 or any(
                row["paused"] or not row["cameraStagesValid"] for row in (a, b)):
            return None
        alpha = (time - a["seconds"]) / duration
        def coordinate(row, axis):
            if prefix == "customOutput":
                return row["cameraPivot" + axis] + row["cameraOffset" + axis]
            return row[prefix + axis]
        return tuple(coordinate(a, axis) * (1 - alpha) + coordinate(b, axis) * alpha
                     for axis in ("X", "Z"))

    def velocity(time, prefix):
        a, b = pose(time - .05, prefix), pose(time + .05, prefix)
        return tuple((y - x) / .1 for x, y in zip(a, b)) if a and b else None

    grid = [times[0] + .1 + index * .01
            for index in range(max(0, int((times[-1] - times[0] - .7) / .01)))]

    def fit(source, destination):
        # Compare the same source timestamps at every candidate delay; no gaps
        # or pause boundaries may be bridged by interpolation.
        candidates = range(0, 201, 10)
        valid = []
        for time in grid:
            source_velocity = velocity(time, source)
            if source_velocity and math.hypot(*source_velocity) > .2:
                targets = [velocity(time + delay / 1000, destination) for delay in candidates]
                if all(targets):
                    valid.append((source_velocity, targets))
        if len(valid) < 50:
            return {"error": "Insufficient unpaused moving samples", "samples": len(valid)}
        scores = []
        for index, delay in enumerate(candidates):
            error = sum(sum((x - y) ** 2 for x, y in zip(source, targets[index]))
                        for source, targets in valid) / len(valid)
            scores.append((math.sqrt(error), delay))
        error, delay = min(scores)
        return {"delayMs": delay, "velocityRMSE": round(error, 4),
                "zeroDelayRMSE": round(scores[0][0], 4), "samples": len(valid)}

    return {"rows": len(rows), "validCameraStageRows": sum(
        bool(row["cameraStagesValid"]) for row in rows), "resolutionMs": 10,
        "feetToPivot": fit("feet", "cameraPivot"),
        "pivotToCustomOutput": fit("cameraPivot", "customOutput"),
        "customOutputToDirector": fit("customOutput", "camera"),
        "feetToDirector": fit("feet", "camera"),
        "limitation": "Velocity-fit estimates; excludes display latency. Use fixed camera orientation."}


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: analyze_camera_stages.py CAPTURE.csv")
    print(json.dumps(analyze(sys.argv[1]), indent=2))
