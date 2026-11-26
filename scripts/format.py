#!/usr/bin/env python3

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

STYLE = """\
BasedOnStyle: LLVM
UseTab: Always
TabWidth: 4
IndentWidth: 4
ContinuationIndentWidth: 4
ColumnLimit: 0
SortIncludes: false
ReflowComments: false
BreakBeforeBraces: Custom
BraceWrapping:
  AfterCaseLabel: false
  AfterClass: false
  AfterControlStatement: Never
  AfterEnum: false
  AfterFunction: false
  AfterNamespace: false
  AfterStruct: false
  AfterUnion: false
  AfterExternBlock: false
  BeforeCatch: true
  BeforeElse: true
  BeforeWhile: false
  IndentBraces: false
  SplitEmptyFunction: false
  SplitEmptyRecord: false
  SplitEmptyNamespace: false
AllowShortBlocksOnASingleLine: Always
AllowShortIfStatementsOnASingleLine: WithoutElse
AllowShortLoopsOnASingleLine: true
AllowShortFunctionsOnASingleLine: None
PointerAlignment: Left
SpaceBeforeParens: ControlStatements
SpacesInParentheses: false
SpacesInSquareBrackets: false
SpaceInEmptyParentheses: false
Cpp11BracedListStyle: false
"""


def collect(paths):
	files = []
	for raw in paths:
		path = Path(raw)
		if path.is_dir():
			files.extend(sorted(path.rglob("*.c")))
			files.extend(sorted(path.rglob("*.h")))
		elif path.is_file():
			files.append(path)
		else:
			print("skip: " + raw, file=sys.stderr)
	return files


def normalize(text):
	lines = text.replace("\r\n", "\n").replace("\r", "\n").split("\n")
	out = []
	for line in lines:
		stripped = line.lstrip()
		if stripped.startswith("else") and out:
			previous = out[-1]
			if previous.strip() and not previous.lstrip().startswith("#"):
				out.append("")
		out.append(line.rstrip())
	while out and not out[-1]:
		out.pop()
	return "\n".join(out) + "\n"


def main(argv):
	parser = argparse.ArgumentParser(description="Format C sources to the project style.")
	parser.add_argument("paths", nargs="*", default=["src"])
	args = parser.parse_args(argv)

	files = collect(args.paths or ["src"])
	if not files:
		print("nothing to format", file=sys.stderr)
		return 1

	with tempfile.TemporaryDirectory() as tmp:
		config = Path(tmp) / ".clang-format"
		config.write_text(STYLE, encoding="utf-8")
		try:
			result = subprocess.run(
				["clang-format", "-i", "-style=file:" + str(config), *map(str, files)],
				check=False,
			)
		except FileNotFoundError:
			print("clang-format not found", file=sys.stderr)
			return 1
		if result.returncode != 0:
			return result.returncode

	for path in files:
		path.write_text(normalize(path.read_text(encoding="utf-8")), encoding="utf-8")

	print("formatted " + str(len(files)) + " file(s)")
	return 0


if __name__ == "__main__":
	raise SystemExit(main(sys.argv[1:]))
