#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


GRAPH_TOKEN_RE = re.compile(r"^@graph\((?P<graph>[A-Za-z_][A-Za-z0-9_]*)\)\s*$")
RULESET_TOKEN_RE = re.compile(r"^@ruleset\((?P<function>[A-Za-z_][A-Za-z0-9_]*)\)\s*$")
FUNC_RE_TEMPLATE = r"\b{function}\s*\("
RULE_ENTRY_RE = re.compile(r"^\s*(?P<kind>Rule|Group|Each)\s*\{\s*(?P<function>[A-Za-z_][A-Za-z0-9_]*)")
GRAPH_DEF_RE = re.compile(
    r"^\s*(?:inline\s+)?(?:static\s+)?constexpr\s+auto\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*=\s*(?P<expr>.*)$"
)
SECTION_COMMENT_RE = re.compile(r"^\s*//\s*@section\s+(?P<title>.*?)\s*$")
IDENTIFIER_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_:]*$")


@dataclass(frozen=True)
class Definition:
    function: str
    file_path: Path
    line_number: int
    comment: str


@dataclass(frozen=True)
class RuleEntry:
    kind: str
    function: str
    comment: str


@dataclass(frozen=True)
class Section:
    title: str
    prefix: str


@dataclass(frozen=True)
class GraphDefinition:
    name: str
    file_path: Path
    line_number: int
    expression: str
    section: Section | None


class GraphNode:
    pass


@dataclass(frozen=True)
class AllNode(GraphNode):
    children: tuple[GraphNode, ...]


@dataclass(frozen=True)
class RuleNode(GraphNode):
    function: str
    comment: str


@dataclass(frozen=True)
class RefNode(GraphNode):
    name: str


@dataclass(frozen=True)
class WrapperNode(GraphNode):
    kind: str
    child: GraphNode


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate spec markdown by expanding @graph(GraphName) and legacy @ruleset(FunctionName) tokens."
    )
    parser.add_argument("input", type=Path, help="Input template markdown file")
    parser.add_argument("output", type=Path, help="Output markdown file to write")
    parser.add_argument(
        "--link-mode",
        choices=("relative", "branch", "sha"),
        default="relative",
        help="How function links should be rendered",
    )
    parser.add_argument(
        "--sha",
        help="Commit SHA to use when --link-mode=sha",
    )
    parser.add_argument(
        "--repo-owner",
        default="tobysharp",
        help="GitHub repository owner for non-relative links",
    )
    parser.add_argument(
        "--repo-name",
        default="hornet",
        help="GitHub repository name for non-relative links",
    )
    parser.add_argument(
        "--source-dir",
        type=Path,
        default=Path("src/hornetlib/consensus/rules"),
        help="Directory to scan for rule function definitions",
    )
    return parser.parse_args()


def find_repo_root(start: Path) -> Path:
    current = start.resolve()
    for candidate in (current, *current.parents):
        if (candidate / ".git").exists():
            return candidate
    raise SystemExit(f"Could not locate repository root from {start}")


def git_output(repo_root: Path, *args: str) -> str:
    try:
        result = subprocess.run(
            ["git", *args],
            cwd=repo_root,
            check=True,
            capture_output=True,
            text=True,
        )
    except subprocess.CalledProcessError as exc:
        stderr = exc.stderr.strip()
        detail = f": {stderr}" if stderr else ""
        raise SystemExit(f"Git command failed: git {' '.join(args)}{detail}") from exc
    return result.stdout.strip()


def validate_link_mode_checkout(repo_root: Path, args: argparse.Namespace) -> None:
    if args.link_mode == "relative":
        return

    head = git_output(repo_root, "rev-parse", "HEAD")

    if args.link_mode == "branch":
        branch = git_output(repo_root, "rev-parse", "--abbrev-ref", "HEAD")
        if branch == "HEAD":
            raise SystemExit(
                "--link-mode=branch requires the local checkout to be on a named branch; "
                "current checkout is detached HEAD."
            )
        return

    assert args.link_mode == "sha"
    assert args.sha is not None
    if not head.startswith(args.sha):
        raise SystemExit(
            "--link-mode=sha requires local HEAD to match the requested SHA; "
            f"HEAD is '{head}' but --sha was '{args.sha}'."
        )


def gather_root_functions(template_text: str) -> list[str]:
    functions: list[str] = []
    for line in template_text.splitlines():
        match = RULESET_TOKEN_RE.match(line.strip())
        if match:
            functions.append(match.group("function"))
    return sorted(set(functions))


def gather_root_graphs(template_text: str) -> list[str]:
    graphs: list[str] = []
    for line in template_text.splitlines():
        match = GRAPH_TOKEN_RE.match(line.strip())
        if match:
            graphs.append(match.group("graph"))
    return sorted(set(graphs))


def is_definition(lines: list[str], start_index: int) -> bool:
    text_parts: list[str] = []
    for index in range(start_index, len(lines)):
        stripped = lines[index].strip()
        text_parts.append(stripped)
        joined = " ".join(text_parts)
        if "{" in joined:
            return True
        if ";" in joined:
            return False
    return False


def extract_comment(lines: list[str], start_index: int) -> str:
    comment_lines: list[str] = []
    index = start_index - 1
    while index >= 0:
      stripped = lines[index].strip()
      if stripped.startswith("//"):
          comment_lines.append(stripped[2:].strip())
          index -= 1
          continue
      break
    comment_lines.reverse()
    return " ".join(part for part in comment_lines if part).strip()


def find_definition(function: str, source_dir: Path) -> Definition:
    pattern = re.compile(FUNC_RE_TEMPLATE.format(function=re.escape(function)))
    matches: list[Definition] = []
    for file_path in sorted(source_dir.rglob("*")):
        if file_path.suffix not in {".h", ".hpp", ".hh", ".hxx", ".ipp", ".cpp", ".cc", ".cxx"}:
            continue
        lines = file_path.read_text(encoding="utf-8").splitlines()
        for index, line in enumerate(lines):
            if not pattern.search(line):
                continue
            if not is_definition(lines, index):
                continue
            comment = extract_comment(lines, index)
            matches.append(
                Definition(
                    function=function,
                    file_path=file_path,
                    line_number=index + 1,
                    comment=comment,
                )
            )
    if not matches:
        raise SystemExit(f"No definition found for function {function}")
    if len(matches) > 1:
        pretty = ", ".join(f"{match.file_path}:{match.line_number}" for match in matches)
        raise SystemExit(f"Multiple definitions found for function {function}: {pretty}")
    return matches[0]


def load_lines(path: Path) -> list[str]:
    return path.read_text(encoding="utf-8").splitlines()


def split_top_level(text: str, delimiter: str = ",") -> list[str]:
    parts: list[str] = []
    start = 0
    depth_paren = 0
    depth_brace = 0
    depth_bracket = 0
    index = 0
    in_line_comment = False

    while index < len(text):
        ch = text[index]
        nxt = text[index + 1] if index + 1 < len(text) else ""

        if in_line_comment:
            if ch == "\n":
                in_line_comment = False
            index += 1
            continue

        if ch == "/" and nxt == "/":
            in_line_comment = True
            index += 2
            continue

        if ch == "(":
            depth_paren += 1
        elif ch == ")":
            depth_paren -= 1
        elif ch == "{":
            depth_brace += 1
        elif ch == "}":
            depth_brace -= 1
        elif ch == "[":
            depth_bracket += 1
        elif ch == "]":
            depth_bracket -= 1
        elif (
            ch == delimiter
            and depth_paren == 0
            and depth_brace == 0
            and depth_bracket == 0
        ):
            end = index
            lookahead = index + 1
            while lookahead < len(text) and text[lookahead] in " \t":
                lookahead += 1
            if text[lookahead:lookahead + 2] == "//":
                comment_end = text.find("\n", lookahead)
                if comment_end == -1:
                    end = len(text)
                    parts.append(text[start:end])
                    start = len(text)
                    break
                end = comment_end
                parts.append(text[start:end])
                start = comment_end + 1
            else:
                parts.append(text[start:index])
                start = index + 1
        index += 1

    tail = text[start:]
    if tail.strip():
        parts.append(tail)
    return parts


def split_code_and_comment(text: str) -> tuple[str, str]:
    index = 0
    depth_paren = 0
    depth_brace = 0
    depth_bracket = 0
    while index < len(text):
        ch = text[index]
        nxt = text[index + 1] if index + 1 < len(text) else ""
        if ch == "/" and nxt == "/" and depth_paren == 0 and depth_brace == 0 and depth_bracket == 0:
            return text[:index], text[index + 2 :]
        if ch == "(":
            depth_paren += 1
        elif ch == ")":
            depth_paren -= 1
        elif ch == "{":
            depth_brace += 1
        elif ch == "}":
            depth_brace -= 1
        elif ch == "[":
            depth_bracket += 1
        elif ch == "]":
            depth_bracket -= 1
        index += 1
    return text, ""


def unwrap_call(text: str) -> tuple[str, str] | None:
    match = re.match(r"^\s*(?P<name>[A-Za-z_][A-Za-z0-9_:]*)\s*(?P<open>[\{\(])", text)
    if not match:
        return None
    name = match.group("name")
    open_char = match.group("open")
    close_char = "}" if open_char == "{" else ")"
    start = match.end()
    depth = 1
    index = start
    while index < len(text):
        ch = text[index]
        if ch == open_char:
            depth += 1
        elif ch == close_char:
            depth -= 1
            if depth == 0:
                remainder = text[index + 1 :].strip()
                if remainder:
                    raise SystemExit(f"Unexpected trailing content after expression: {remainder!r}")
                return name, text[start:index]
        index += 1
    raise SystemExit(f"Unterminated expression: {text!r}")


def parse_graph_expression(text: str) -> GraphNode:
    code, comment = split_code_and_comment(text)
    code = code.strip().rstrip(",").strip()
    comment = comment.strip()
    if not code:
        raise SystemExit("Encountered empty graph expression")

    if IDENTIFIER_RE.match(code):
        return RefNode(code)

    unwrapped = unwrap_call(code)
    if unwrapped is None:
        raise SystemExit(f"Unsupported graph expression: {code!r}")

    name, inner = unwrapped
    args = [part.strip() for part in split_top_level(inner) if part.strip()]

    if name == "All":
        return AllNode(tuple(parse_graph_expression(arg) for arg in args))
    if name == "Rule":
        if len(args) != 1:
            raise SystemExit(f"Rule expects exactly one argument, got {len(args)}")
        return RuleNode(function=args[0], comment=comment)
    if name == "With":
        if len(args) != 2:
            raise SystemExit(f"With expects exactly two arguments, got {len(args)}")
        return WrapperNode(kind=name, child=parse_graph_expression(args[1]))
    if name == "When":
        if len(args) != 2:
            raise SystemExit(f"When expects exactly two arguments, got {len(args)}")
        return WrapperNode(kind=name, child=parse_graph_expression(args[1]))
    if name == "Each":
        if not args:
            raise SystemExit("Each expects at least one argument")
        return WrapperNode(kind=name, child=parse_graph_expression(args[-1]))
    if name == "From":
        if len(args) != 2:
            raise SystemExit(f"From expects exactly two arguments, got {len(args)}")
        child = parse_graph_expression(args[0]) if not IDENTIFIER_RE.match(args[0]) else RuleNode(args[0], comment)
        return WrapperNode(kind=name, child=child)

    raise SystemExit(f"Unsupported graph node kind: {name}")


def find_graph_expression(lines: list[str], start_index: int, initial_expr: str) -> str:
    text_parts = [initial_expr]
    index = start_index
    depth_paren = 0
    depth_brace = 0
    depth_bracket = 0
    in_line_comment = False
    seen_structure = False

    while index < len(lines):
        text = text_parts[-1] if index == start_index else lines[index]
        scan_text = text + ("\n" if index < len(lines) - 1 else "")
        pos = 0
        while pos < len(scan_text):
            ch = scan_text[pos]
            nxt = scan_text[pos + 1] if pos + 1 < len(scan_text) else ""
            if in_line_comment:
                if ch == "\n":
                    in_line_comment = False
                pos += 1
                continue
            if ch == "/" and nxt == "/":
                in_line_comment = True
                pos += 2
                continue
            if ch == "(":
                depth_paren += 1
                seen_structure = True
            elif ch == ")":
                depth_paren -= 1
            elif ch == "{":
                depth_brace += 1
                seen_structure = True
            elif ch == "}":
                depth_brace -= 1
            elif ch == "[":
                depth_bracket += 1
            elif ch == "]":
                depth_bracket -= 1
            elif ch == ";" and seen_structure and depth_paren == 0 and depth_brace == 0 and depth_bracket == 0:
                full_text = "\n".join(text_parts)
                return full_text.rsplit(";", 1)[0].strip()
            pos += 1
        index += 1
        if index < len(lines):
            text_parts.append(lines[index])
    raise SystemExit("Could not find the end of graph expression")


def extract_section_comment(lines: list[str], start_index: int) -> Section | None:
    index = start_index - 1
    while index >= 0:
        stripped = lines[index].strip()
        if not stripped:
            index -= 1
            continue
        match = SECTION_COMMENT_RE.match(stripped)
        if match:
            return section_from_title(match.group("title"))
        if stripped.startswith("//"):
            return None
        return None
    return None


def find_graph_definition(name: str, source_dir: Path) -> GraphDefinition:
    matches: list[GraphDefinition] = []
    for file_path in sorted(source_dir.rglob("*")):
        if file_path.suffix not in {".h", ".hpp", ".hh", ".hxx", ".ipp", ".cpp", ".cc", ".cxx"}:
            continue
        lines = load_lines(file_path)
        for index, line in enumerate(lines):
            match = GRAPH_DEF_RE.match(line)
            if not match or match.group("name") != name:
                continue
            expression = find_graph_expression(lines, index, match.group("expr"))
            section = extract_section_comment(lines, index)
            matches.append(
                GraphDefinition(
                    name=name,
                    file_path=file_path,
                    line_number=index + 1,
                    expression=expression,
                    section=section,
                )
            )
    if not matches:
        raise SystemExit(f"No graph definition found for {name}")
    if len(matches) > 1:
        pretty = ", ".join(f"{match.file_path}:{match.line_number}" for match in matches)
        raise SystemExit(f"Multiple graph definitions found for {name}: {pretty}")
    return matches[0]


def find_ruleset_lines(lines: list[str], start_index: int) -> list[str] | None:
    ruleset_start = None
    for index in range(start_index, len(lines)):
        if "std::make_tuple(" in lines[index]:
            ruleset_start = index
            break
        if lines[index].strip().startswith("return "):
            break
    if ruleset_start is None:
        return None

    collected: list[str] = []
    depth = 0
    started = False
    for index in range(ruleset_start, len(lines)):
        line = lines[index]
        if not started:
            marker = "std::make_tuple("
            marker_pos = line.find(marker)
            if marker_pos == -1:
                continue
            fragment = line[marker_pos + len(marker):]
            depth = 1
            started = True
        else:
            fragment = line

        code_fragment = fragment.split("//", 1)[0]
        depth += code_fragment.count("(") - code_fragment.count(")")
        collected.append(line)

        if started and depth == 0:
            break

    if not collected:
        return None

    if collected[-1].rstrip().endswith(");"):
        collected[-1] = collected[-1].rsplit(");", 1)[0]
    return collected


def parse_ruleset_entries(lines: list[str]) -> list[RuleEntry]:
    entries: list[RuleEntry] = []
    for raw_line in lines:
        code, _, comment = raw_line.partition("//")
        match = RULE_ENTRY_RE.match(code)
        if not match:
            continue
        entries.append(
            RuleEntry(
                kind=match.group("kind"),
                function=match.group("function"),
                comment=comment.strip(),
            )
        )
    return entries


def section_from_title(title: str) -> Section:
    match = re.search(r"[A-Za-z]", title)
    if not match:
        raise SystemExit(f"Could not determine section prefix from title: {title!r}")
    return Section(title=title, prefix=match.group(0).upper())


def heading_lines(title: str) -> list[str]:
    return ["|", f"||**{title}**", "|"]


def make_link(definition: Definition, output_dir: Path, repo_root: Path, args: argparse.Namespace) -> str:
    rel_to_root = definition.file_path.resolve().relative_to(repo_root)
    if args.link_mode == "relative":
        target = os.path.relpath(definition.file_path.resolve(), output_dir)
        return f"{target}#L{definition.line_number}"
    if args.link_mode == "branch":
        branch = git_output(repo_root, "rev-parse", "--abbrev-ref", "HEAD")
        return (
            f"https://github.com/{args.repo_owner}/{args.repo_name}/blob/{branch}/"
            f"{rel_to_root.as_posix()}#L{definition.line_number}"
        )
    if not args.sha:
        raise SystemExit("--sha is required when --link-mode=sha")
    return (
        f"https://github.com/{args.repo_owner}/{args.repo_name}/blob/{args.sha}/"
        f"{rel_to_root.as_posix()}#L{definition.line_number}"
    )


def render_leaf(
    definition: Definition,
    section: Section,
    counters: dict[str, int],
    output_dir: Path,
    repo_root: Path,
    args: argparse.Namespace,
) -> str:
    counters.setdefault(section.prefix, 0)
    counters[section.prefix] += 1
    rule_id = f"{section.prefix}{counters[section.prefix]:02d}"
    link = make_link(definition, output_dir, repo_root, args)
    return f"{rule_id}|{definition.comment}|[`{definition.function}`]({link})"


def resolve_leaf_comment(node_comment: str, definition: Definition) -> str:
    if node_comment:
        return node_comment
    if definition.comment:
        return definition.comment
    raise SystemExit(f"No rule prose found for function {definition.function}")


def walk_graph_node(
    node: GraphNode,
    current_section: Section | None,
    definitions: dict[str, Definition],
    graphs: dict[str, GraphDefinition],
    parsed_graphs: dict[str, GraphNode],
    output_lines: list[str],
    counters: dict[str, int],
    output_dir: Path,
    repo_root: Path,
    source_dir: Path,
    args: argparse.Namespace,
) -> None:
    if isinstance(node, AllNode):
        for child in node.children:
            walk_graph_node(
                child,
                current_section,
                definitions,
                graphs,
                parsed_graphs,
                output_lines,
                counters,
                output_dir,
                repo_root,
                source_dir,
                args,
            )
        return

    if isinstance(node, WrapperNode):
        walk_graph_node(
            node.child,
            current_section,
            definitions,
            graphs,
            parsed_graphs,
            output_lines,
            counters,
            output_dir,
            repo_root,
            source_dir,
            args,
        )
        return

    if isinstance(node, RefNode):
        walk_graph(
            node.name,
            current_section,
            False,
            definitions,
            graphs,
            parsed_graphs,
            counters,
            output_lines,
            output_dir,
            repo_root,
            source_dir,
            args,
        )
        return

    if isinstance(node, RuleNode):
        if current_section is None:
            raise SystemExit(f"Rule leaf {node.function} was reached without an active section")
        definitions.setdefault(node.function, find_definition(node.function, source_dir))
        definition = definitions[node.function]
        rendered = render_leaf(
            Definition(
                function=definition.function,
                file_path=definition.file_path,
                line_number=definition.line_number,
                comment=resolve_leaf_comment(node.comment, definition),
            ),
            current_section,
            counters,
            output_dir,
            repo_root,
            args,
        )
        output_lines.append(rendered)
        return

    raise SystemExit(f"Unsupported graph node type: {type(node).__name__}")


def walk_graph(
    graph_name: str,
    current_section: Section | None,
    is_root: bool,
    definitions: dict[str, Definition],
    graphs: dict[str, GraphDefinition],
    parsed_graphs: dict[str, GraphNode],
    counters: dict[str, int],
    output_lines: list[str],
    output_dir: Path,
    repo_root: Path,
    source_dir: Path,
    args: argparse.Namespace,
) -> None:
    graph = graphs.setdefault(graph_name, find_graph_definition(graph_name, source_dir))
    if graph_name not in parsed_graphs:
        parsed_graphs[graph_name] = parse_graph_expression(graph.expression)

    section = graph.section or current_section
    if graph.section and graph.section != current_section:
        output_lines.extend(heading_lines(graph.section.title))

    walk_graph_node(
        parsed_graphs[graph_name],
        section,
        definitions,
        graphs,
        parsed_graphs,
        output_lines,
        counters,
        output_dir,
        repo_root,
        source_dir,
        args,
    )


def walk_ruleset(
    function: str,
    current_section: Section | None,
    is_root: bool,
    definitions: dict[str, Definition],
    lines_cache: dict[Path, list[str]],
    counters: dict[str, int],
    output_lines: list[str],
    output_dir: Path,
    repo_root: Path,
    source_dir: Path,
    args: argparse.Namespace,
) -> None:
    definition = definitions[function]
    lines = lines_cache.setdefault(definition.file_path, load_lines(definition.file_path))
    ruleset_lines = find_ruleset_lines(lines, definition.line_number - 1)
    if ruleset_lines is None:
        if is_root:
            raise SystemExit(f"Root function {function} does not define a ruleset")
        if current_section is None:
            raise SystemExit(f"Leaf function {function} was reached without an active section")
        output_lines.append(render_leaf(definition, current_section, counters, output_dir, repo_root, args))
        return

    entries = parse_ruleset_entries(ruleset_lines)
    if not entries:
        raise SystemExit(f"No ruleset entries found in function {function}")

    section = current_section

    if section is not None and current_section != section:
        output_lines.extend(heading_lines(section.title))

    for entry in entries:
        definitions.setdefault(entry.function, find_definition(entry.function, source_dir))
        entry_section = section
        if entry_section is None and entry.comment:
            entry_section = section_from_title(entry.comment)
            output_lines.extend(heading_lines(entry_section.title))
        if entry.kind == "Rule":
            if entry_section is None:
                raise SystemExit(f"Rule leaf {entry.function} was reached without an active section")
            output_lines.append(
                render_leaf(definitions[entry.function], entry_section, counters, output_dir, repo_root, args)
            )
            continue
        if entry.kind in {"Group", "Each"}:
            walk_ruleset(
                entry.function,
                entry_section,
                False,
                definitions,
                lines_cache,
                counters,
                output_lines,
                output_dir,
                repo_root,
                source_dir,
                args,
            )
            continue
        raise SystemExit(f"Unsupported ruleset entry kind: {entry.kind}")


def expand_template(template_text: str, definitions: dict[str, Definition], repo_root: Path, args: argparse.Namespace) -> str:
    output_lines: list[str] = []
    output_dir = args.output.resolve().parent
    source_dir = (repo_root / args.source_dir).resolve() if not args.source_dir.is_absolute() else args.source_dir.resolve()
    lines_cache = {definition.file_path: load_lines(definition.file_path) for definition in definitions.values()}
    counters: dict[str, int] = {}
    graphs: dict[str, GraphDefinition] = {}
    parsed_graphs: dict[str, GraphNode] = {}
    for line in template_text.splitlines():
        graph_match = GRAPH_TOKEN_RE.match(line.strip())
        if graph_match:
            walk_graph(
                graph_match.group("graph"),
                None,
                True,
                definitions,
                graphs,
                parsed_graphs,
                counters,
                output_lines,
                output_dir,
                repo_root,
                source_dir,
                args,
            )
            continue
        match = RULESET_TOKEN_RE.match(line.strip())
        if not match:
            output_lines.append(line)
            continue
        function = match.group("function")
        walk_ruleset(
            function,
            None,
            True,
            definitions,
            lines_cache,
            counters,
            output_lines,
            output_dir,
            repo_root,
            source_dir,
            args,
        )
    return "\n".join(output_lines) + "\n"


def main() -> int:
    args = parse_args()
    if args.link_mode == "sha" and not args.sha:
        raise SystemExit("--sha is required when --link-mode=sha")

    repo_root = find_repo_root(args.input.parent)
    validate_link_mode_checkout(repo_root, args)
    input_path = (Path.cwd() / args.input).resolve()
    output_path = (Path.cwd() / args.output).resolve()
    source_dir = (repo_root / args.source_dir).resolve()
    template_text = input_path.read_text(encoding="utf-8")
    functions = gather_root_functions(template_text)
    _graphs = gather_root_graphs(template_text)
    definitions = {function: find_definition(function, source_dir) for function in functions}
    expanded = expand_template(template_text, definitions, repo_root, args)
    output_path.write_text(expanded, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())