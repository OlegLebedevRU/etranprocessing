"""Offline timestamp-only cadence vectors; not an AU/GOP parser."""
import json


def candidates(timestamps, width=2592, height=1944):
    if len(timestamps) < 2 or timestamps[-1] - timestamps[0] < 360000:
        return []
    if any(b <= a for a, b in zip(timestamps, timestamps[1:])):
        return []
    maximum = 20 if width > 1920 or height > 1080 else 30
    return [
        fps for fps in range(1, maximum + 1)
        if all(abs(fps * (t - timestamps[0]) - 90000 * i) <= 45000 + fps
               for i, t in enumerate(timestamps))
    ]


def main():
    cases = [
        ("constant20", [4500*i for i in range(101)], 2592, 1944, [20]),
        ("bounded20", [4500*i-(1500 if i % 2 else 0) for i in range(101)], 2592, 1944, [20]),
        ("short_prefix", [4500*i for i in range(40)], 2592, 1944, []),
        ("drift", [4540*i for i in range(101)], 2592, 1944, []),
        ("large_geometry25", [3600*i for i in range(126)], 2592, 1944, []),
        ("fullhd25", [3600*i for i in range(126)], 1920, 1080, [25]),
        ("duplicate", [4500*i for i in range(100)]+[445500], 2592, 1944, []),
    ]
    for name, ts, width, height, expected in cases:
        actual = candidates(ts, width, height)
        assert actual == expected, (name, actual, expected)
    print(json.dumps({"passed": len(cases), "runtime_admission": False}))


if __name__ == "__main__":
    main()
