#!/usr/bin/env python3
"""Reduce #if/#elif conditions that mix Windows symbols with other tests.

unifdef only rewrites a condition when the whole expression becomes constant,
so a line such as

    #if defined(__LP64__) || defined(_WIN64) || defined(__aarch64__)

is left exactly as it was, still naming a Windows symbol. This script takes
each #if/#elif that names a symbol from windows-macros.txt and rewrites it
using the one substitution that is exact by definition: on Linux those symbols
are undefined, so `defined(W)`, `defined W` and a bare `W` all evaluate to 0
(C11 6.10.1p4). The expression is then folded:

  - constant sub-expressions are evaluated (as intmax_t; anything that would
    overflow, divide by zero or needs unsigned arithmetic is left alone);
  - `0 && x`, `x && 0` -> 0 and `1 || x`, `x || 1` -> 1 (preprocessor
    expressions have no side effects, so dropping x is exact);
  - `1 && x`, `0 || x` -> x, but only where the value is used as a truth value
    (the condition itself, or an operand of !, &&, || or ?:'s test) -- in
    arithmetic `1 && x` is 0 or 1 while x may be any number.

If the whole condition becomes constant, it is replaced by
`defined(LINUX_ONLY_TRUE)` or `defined(LINUX_ONLY_FALSE)` and filter-tree.sh
runs unifdef once more with those two markers, so the dead branch is deleted by
unifdef's own #elif/#else handling. Otherwise the reduced expression, which no
longer names a Windows symbol, replaces the original. A condition this parser
does not understand is left untouched, and check-tree.sh reports it.

Usage: simplify_conditions.py [--keep-lines] <macros-file> <file>...
  --keep-lines   when a backslash-continued directive is rewritten onto one
                 line, pad with blank lines so later line numbers do not move.
Rewrites files in place and prints one line per file it changed.
"""

import re
import sys

TRUE_MARK = "LINUX_ONLY_TRUE"
FALSE_MARK = "LINUX_ONLY_FALSE"
INTMAX = 2 ** 63


class ParseError(Exception):
    pass

# ---------------------------------------------------------------- tokenizer


TOKEN_RE = re.compile(r"""
    \s*(?:
      (?P<num>(?:0[xX][0-9a-fA-F]+|[0-9]+)[uUlL]*)
    | (?P<ident>[A-Za-z_][A-Za-z0-9_]*)
    | (?P<op>\|\||&&|==|!=|<=|>=|<<|>>|[!~+\-*/%<>&|^?:()])
    )""", re.VERBOSE)


def tokenize(text):
    pos, out, text = 0, [], text.strip()
    while pos < len(text):
        m = TOKEN_RE.match(text, pos)
        if not m or m.end() == pos:
            raise ParseError("unexpected character %r" % text[pos])
        pos = m.end()
        if m.group("num") is not None:
            raw = m.group("num")
            digits = raw.rstrip("uUlL")
            if any(c in "uU" for c in raw[len(digits):]):
                raise ParseError("unsigned literal")
            try:
                if digits[:2] in ("0x", "0X"):
                    value = int(digits, 16)
                elif len(digits) > 1 and digits[0] == "0":
                    value = int(digits, 8)
                else:
                    value = int(digits)
            except ValueError:
                raise ParseError("bad literal %r" % raw)
            if value >= INTMAX:
                raise ParseError("literal beyond intmax_t")
            out.append(("num", value))
        elif m.group("ident") is not None:
            out.append(("ident", m.group("ident")))
        else:
            out.append(("op", m.group("op")))
    return out

# ------------------------------------------------------------------- parser
# AST: ("num", v) ("ident", name) ("defined", name)
#      ("un", op, a) ("bin", op, a, b) ("tern", cond, a, b)

BINARY_PREC = {
    "||": 1, "&&": 2, "|": 3, "^": 4, "&": 5,
    "==": 6, "!=": 6, "<": 7, ">": 7, "<=": 7, ">=": 7,
    "<<": 8, ">>": 8, "+": 9, "-": 9, "*": 10, "/": 10, "%": 10,
}
UNARY_PREC = 11
ATOM_PREC = 12


class Parser:
    def __init__(self, toks):
        self.toks, self.i = toks, 0

    def peek(self):
        return self.toks[self.i] if self.i < len(self.toks) else (None, None)

    def take(self, kind=None, val=None):
        tok = self.peek()
        if tok[0] is None or (kind and tok[0] != kind) or (val and tok[1] != val):
            raise ParseError("expected %s %s, got %r" % (kind, val, tok))
        self.i += 1
        return tok

    def parse(self):
        if not self.toks:
            raise ParseError("empty condition")
        node = self.ternary()
        if self.i != len(self.toks):
            raise ParseError("trailing tokens")
        return node

    def ternary(self):
        cond = self.binary(1)
        if self.peek() == ("op", "?"):
            self.take()
            a = self.ternary()
            self.take("op", ":")
            b = self.ternary()
            return ("tern", cond, a, b)
        return cond

    def binary(self, min_prec):
        left = self.unary()
        while True:
            kind, op = self.peek()
            if kind != "op" or op not in BINARY_PREC or BINARY_PREC[op] < min_prec:
                return left
            self.take()
            left = ("bin", op, left, self.binary(BINARY_PREC[op] + 1))

    def unary(self):
        kind, val = self.peek()
        if kind == "op" and val in ("!", "~", "-", "+"):
            self.take()
            return ("un", val, self.unary())
        if kind == "op" and val == "(":
            self.take()
            node = self.ternary()
            self.take("op", ")")
            return node
        if kind == "num":
            self.take()
            return ("num", val)
        if kind == "ident" and val == "defined":
            self.take()
            if self.peek() == ("op", "("):
                self.take()
                name = self.take("ident")[1]
                self.take("op", ")")
            else:
                name = self.take("ident")[1]
            return ("defined", name)
        if kind == "ident":
            self.take()
            if self.peek() == ("op", "("):
                raise ParseError("function-like macro in condition")  # __has_include(...)
            return ("ident", val)
        raise ParseError("unexpected %r" % (self.peek(),))

# --------------------------------------------------------------- simplifier


def c_div(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def c_binop(op, a, b):
    """C preprocessor arithmetic on intmax_t; None where that is not exact."""
    if op in ("/", "%") and b == 0:
        return None
    if op in ("<<", ">>") and not 0 <= b < 63:
        return None
    if op == "/":
        r = c_div(a, b)
    elif op == "%":
        r = a - b * c_div(a, b)
    else:
        r = {
            "||": lambda: int(bool(a) or bool(b)), "&&": lambda: int(bool(a) and bool(b)),
            "|": lambda: a | b, "^": lambda: a ^ b, "&": lambda: a & b,
            "==": lambda: int(a == b), "!=": lambda: int(a != b),
            "<": lambda: int(a < b), ">": lambda: int(a > b),
            "<=": lambda: int(a <= b), ">=": lambda: int(a >= b),
            "<<": lambda: a << b, ">>": lambda: a >> b,
            "+": lambda: a + b, "-": lambda: a - b, "*": lambda: a * b,
        }[op]()
    return r if -INTMAX <= r < INTMAX else None


def simplify(node, windows, boolctx):
    kind = node[0]
    if kind == "num":
        return node
    if kind in ("defined", "ident"):
        return ("num", 0) if node[1] in windows else node
    if kind == "un":
        op = node[1]
        a = simplify(node[2], windows, op == "!")
        if a[0] == "num":
            v = {"!": int(not a[1]), "~": ~a[1], "-": -a[1], "+": a[1]}[op]
            if -INTMAX <= v < INTMAX:
                return ("num", v)
        return ("un", op, a)
    if kind == "tern":
        cond = simplify(node[1], windows, True)
        if cond[0] == "num":
            return simplify(node[2] if cond[1] else node[3], windows, boolctx)
        return ("tern", cond, simplify(node[2], windows, boolctx),
                simplify(node[3], windows, boolctx))
    op = node[1]
    logical = op in ("&&", "||")
    a = simplify(node[2], windows, logical)
    b = simplify(node[3], windows, logical)
    if a[0] == "num" and b[0] == "num":
        v = c_binop(op, a[1], b[1])
        return ("num", v) if v is not None else ("bin", op, a, b)
    if logical:
        absorbing = 0 if op == "&&" else 1  # the value that decides the result
        for const, other in ((a, b), (b, a)):
            if const[0] != "num":
                continue
            if bool(const[1]) == bool(absorbing):
                return ("num", absorbing)
            if boolctx:
                return other  # 1 && x == x and 0 || x == x, as truth values
    return ("bin", op, a, b)


def mentions(node, windows):
    if node[0] in ("defined", "ident"):
        return node[1] in windows
    return any(mentions(n, windows) for n in node[1:] if isinstance(n, tuple))

# ------------------------------------------------------------------ printer


def prec(node):
    return {"tern": 0, "un": UNARY_PREC}.get(node[0], BINARY_PREC.get(node[1]) if node[0] == "bin" else ATOM_PREC)


def emit(node, parent_prec=0, right=False):
    kind = node[0]
    if kind == "num":
        s = str(node[1])
    elif kind == "ident":
        s = node[1]
    elif kind == "defined":
        s = "defined(%s)" % node[1]
    elif kind == "un":
        s = node[1] + emit(node[2], UNARY_PREC)
    elif kind == "tern":
        s = "%s ? %s : %s" % (emit(node[1], 1), emit(node[2]), emit(node[3]))
    else:
        p = BINARY_PREC[node[1]]
        s = "%s %s %s" % (emit(node[2], p), node[1], emit(node[3], p, right=True))
    p = prec(node)
    if p < parent_prec or (right and p == parent_prec):
        s = "(" + s + ")"
    return s

# ---------------------------------------------------------------- file pass


DIRECTIVE_RE = re.compile(r"^(\s*#\s*)(if|elif)\b(.*)$", re.DOTALL)


def split_comments(text):
    """Return (code, trailing): comments removed from code; the last comment
    kept as `trailing` when nothing but whitespace follows it."""
    code, trailing, i = [], "", 0
    while i < len(text):
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            if end < 0:
                raise ParseError("unterminated comment")
            comment = text[i:end + 2]
            i = end + 2
            code.append(" ")
            trailing = comment if text[i:].strip() == "" else trailing
        elif text.startswith("//", i):
            trailing = text[i:].rstrip()
            break
        else:
            code.append(text[i])
            i += 1
    return "".join(code), trailing


def directive_span(lines, i):
    """Index of the last physical line of the directive starting at i."""
    j = i
    while lines[j].rstrip("\r\n").endswith("\\") and j + 1 < len(lines):
        j += 1
    return j


def rewrite(span, windows):
    """New text for one directive, or None to keep it as it is."""
    eol = "\r\n" if span[-1].endswith("\r\n") else "\n" if span[-1].endswith("\n") else ""
    logical = "".join(l.rstrip("\r\n")[:-1] if l.rstrip("\r\n").endswith("\\")
                      else l.rstrip("\r\n") for l in span)
    m = DIRECTIVE_RE.match(logical)
    try:
        code, trailing = split_comments(m.group(3))
        tree = Parser(tokenize(code)).parse()
    except ParseError:
        return None
    if not mentions(tree, windows):
        return None
    reduced = simplify(tree, windows, True)
    if reduced[0] == "num":
        expr = "defined(%s)" % (TRUE_MARK if reduced[1] else FALSE_MARK)
    else:
        expr = emit(reduced)
    return "%s%s %s%s%s" % (m.group(1), m.group(2), expr,
                            (" " + trailing) if trailing else "", eol), eol


def process(path, windows, keep_lines):
    with open(path, encoding="utf-8", errors="surrogateescape", newline="") as fh:
        lines = fh.readlines()
    word = re.compile(r"\b(?:%s)\b" % "|".join(map(re.escape, sorted(windows))))
    out, i, changed = [], 0, 0
    while i < len(lines):
        if not DIRECTIVE_RE.match(lines[i]):
            out.append(lines[i])
            i += 1
            continue
        j = directive_span(lines, i)
        span = lines[i:j + 1]
        result = rewrite(span, windows) if word.search("".join(span)) else None
        if result is None:
            out.extend(span)
        else:
            new, eol = result
            out.append(new)
            if keep_lines:
                out.extend([eol or "\n"] * (len(span) - 1))
            changed += 1
        i = j + 1
    if changed:
        with open(path, "w", encoding="utf-8", errors="surrogateescape", newline="") as fh:
            fh.writelines(out)
    return changed


def main(argv):
    keep_lines = False
    if argv and argv[0] == "--keep-lines":
        keep_lines, argv = True, argv[1:]
    if not argv:
        print(__doc__, file=sys.stderr)
        return 2
    with open(argv[0]) as fh:
        windows = {l.split("#")[0].strip() for l in fh} - {""}
    for path in argv[1:]:
        n = process(path, windows, keep_lines)
        if n:
            print("%s: %d condition(s) reduced" % (path, n))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
