import json
import os
import subprocess
from pathlib import Path

from components import select_components


def main():
    event = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text())
    paths = []
    if os.environ["GITHUB_EVENT_NAME"] == "push":
        before = event["before"]
        if set(before) == {"0"}:
            before = (
                subprocess.check_output(
                    ["git", "hash-object", "-t", "tree", "--stdin"], input=b""
                )
                .decode()
                .strip()
            )
        paths = (
            subprocess.check_output(
                [
                    "git",
                    "diff",
                    "--name-only",
                    "-z",
                    before,
                    os.environ["GITHUB_SHA"],
                    "--",
                    "MenuBuilder",
                    "ProcessingBackend/backend",
                    "shared",
                    "l4media",
                    ".github",
                    ".dockerignore",
                    ".gitattributes",
                ]
            )
            .decode()
            .split("\0")
        )
    matrix = select_components(
        paths,
        os.environ["GITHUB_EVENT_NAME"],
        event.get("inputs", {}).get("component", "all"),
        os.environ.get("CI_BOOTSTRAP_COMPONENT", ""),
    )
    print(json.dumps(matrix, indent=2))
    with Path(os.environ["GITHUB_OUTPUT"]).open("a") as output:
        output.write("matrix=" + json.dumps(matrix, separators=(",", ":")) + "\n")
    with Path(os.environ["GITHUB_STEP_SUMMARY"]).open("a") as summary:
        selected = [item["name"] for item in matrix["include"] if item["trigger"]]
        summary.write(
            "### Selected components\n"
            + "\n".join(f"- {name}" for name in selected)
            + "\n"
        )
        if os.environ.get("CI_BOOTSTRAP_COMPONENT"):
            summary.write(
                "- Bootstrap restriction active; "
                "remove CI_BOOTSTRAP_COMPONENT after the trial.\n"
            )


if __name__ == "__main__":
    main()
