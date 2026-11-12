"""从一份冻结矩阵生成 C++/Python CI 与联合发布预期集合。"""

import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("kind", choices=["cpp", "wheel"])
    args = parser.parse_args()
    config = json.loads((ROOT / "python/release-matrix.json").read_text())
    entries = []
    for platform in config["platforms"]:
        if args.kind == "wheel":
            entries.append(platform)
        else:
            for linkage in config["linkages"]:
                entries.append(
                    {
                        **platform,
                        "linkage": linkage,
                        "shared": "ON" if linkage == "shared" else "OFF",
                    }
                )
    print(json.dumps({"include": entries}, separators=(",", ":")))


if __name__ == "__main__":
    main()
