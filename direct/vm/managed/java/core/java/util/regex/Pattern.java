package java.util.regex;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * A backtracking regular expression engine with java.util.regex syntax:
 * classes (with ranges, nesting, intersection and \p{...} properties), greedy,
 * lazy and possessive quantifiers, capturing, named, non-capturing and atomic
 * groups, lookahead and lookbehind, backreferences, \Q...\E, inline flags and
 * the CASE_INSENSITIVE, MULTILINE, DOTALL, UNIX_LINES, COMMENTS and LITERAL
 * flags. Characters are UTF-16 units. Matching uses continuations; loops over
 * a single character run iteratively, so long inputs do not recurse deeply.
 */
public final class Pattern implements java.io.Serializable {
    public static final int UNIX_LINES = 0x01;
    public static final int CASE_INSENSITIVE = 0x02;
    public static final int COMMENTS = 0x04;
    public static final int MULTILINE = 0x08;
    public static final int LITERAL = 0x10;
    public static final int DOTALL = 0x20;
    public static final int UNICODE_CASE = 0x40;
    public static final int CANON_EQ = 0x80;
    public static final int UNICODE_CHARACTER_CLASS = 0x100;

    private final String pattern;
    private final int flags;
    final Node root;
    final int groupCount;
    final Map<String, Integer> namedGroups;

    private Pattern(String regex, int flags) {
        if (regex == null) throw new NullPointerException("regex == null");
        this.pattern = regex;
        this.flags = flags;
        Parser p = new Parser(regex, flags);
        this.root = p.parseAll();
        this.groupCount = p.groups;
        this.namedGroups = p.names;
    }

    public static Pattern compile(String regex) {
        return new Pattern(regex, 0);
    }

    public static Pattern compile(String regex, int flags) {
        return new Pattern(regex, flags);
    }

    public String pattern() {
        return pattern;
    }

    public String toString() {
        return pattern;
    }

    public int flags() {
        return flags;
    }

    public Matcher matcher(CharSequence input) {
        return new Matcher(this, input);
    }

    public static boolean matches(String regex, CharSequence input) {
        return compile(regex).matcher(input).matches();
    }

    public String[] split(CharSequence input) {
        return split(input, 0);
    }

    public String[] split(CharSequence input, int limit) {
        int index = 0;
        boolean matchLimited = limit > 0;
        ArrayList<String> matchList = new ArrayList<String>();
        Matcher m = matcher(input);
        while (m.find()) {
            if (!matchLimited || matchList.size() < limit - 1) {
                if (index == 0 && index == m.start() && m.start() == m.end()) continue;
                matchList.add(input.subSequence(index, m.start()).toString());
                index = m.end();
            } else if (matchList.size() == limit - 1) {
                matchList.add(input.subSequence(index, input.length()).toString());
                index = m.end();
            }
        }
        if (index == 0) return new String[] {input.toString()};
        if (!matchLimited || matchList.size() < limit) matchList.add(input.subSequence(index, input.length()).toString());
        int resultSize = matchList.size();
        if (limit == 0) {
            while (resultSize > 0 && matchList.get(resultSize - 1).isEmpty()) resultSize--;
        }
        String[] result = new String[resultSize];
        return matchList.subList(0, resultSize).toArray(result);
    }

    public static String quote(String s) {
        int slashEIndex = s.indexOf("\\E");
        if (slashEIndex == -1) return "\\Q" + s + "\\E";
        StringBuilder sb = new StringBuilder(s.length() * 2);
        sb.append("\\Q");
        int current = 0;
        while ((slashEIndex = s.indexOf("\\E", current)) != -1) {
            sb.append(s, current, slashEIndex);
            current = slashEIndex + 2;
            sb.append("\\E\\\\E\\Q");
        }
        sb.append(s, current, s.length());
        sb.append("\\E");
        return sb.toString();
    }

    public java.util.function.Predicate<String> asPredicate() {
        return new java.util.function.Predicate<String>() {
            public boolean test(String s) {
                return matcher(s).find();
            }
        };
    }

    // --- matching nodes ---------------------------------------------------------------

    abstract static class Cont {
        abstract boolean run(int i);
    }

    static final Cont ANY = new Cont() {
        boolean run(int i) {
            return true;
        }
    };

    abstract static class Node {
        abstract boolean match(Matcher m, int i, Cont k);

        /** The most characters this node can consume, or -1 if unbounded. */
        int maxWidth() {
            return -1;
        }
    }

    abstract static class CharPred {
        abstract boolean is(int c);
    }

    static final class CharNode extends Node {
        final CharPred pred;

        CharNode(CharPred pred) {
            this.pred = pred;
        }

        boolean match(Matcher m, int i, Cont k) {
            if (i >= m.to) {
                m.hitEnd = true;
                return false;
            }
            return pred.is(m.text.charAt(i)) && k.run(i + 1);
        }

        int maxWidth() {
            return 1;
        }
    }

    static final class Empty extends Node {
        boolean match(Matcher m, int i, Cont k) {
            return k.run(i);
        }

        int maxWidth() {
            return 0;
        }
    }

    static final class Seq extends Node {
        final Node[] items;

        Seq(List<Node> items) {
            this.items = items.toArray(new Node[items.size()]);
        }

        boolean match(Matcher m, int i, Cont k) {
            return step(m, 0, i, k);
        }

        boolean step(final Matcher m, int idx, int i, final Cont k) {
            // Runs of single characters cannot backtrack internally.
            while (idx < items.length && items[idx] instanceof CharNode) {
                if (i >= m.to) {
                    m.hitEnd = true;
                    return false;
                }
                if (!((CharNode) items[idx]).pred.is(m.text.charAt(i))) return false;
                i++;
                idx++;
            }
            if (idx == items.length) return k.run(i);
            if (idx == items.length - 1) return items[idx].match(m, i, k);
            final int next = idx + 1;
            return items[idx].match(m, i, new Cont() {
                boolean run(int j) {
                    return step(m, next, j, k);
                }
            });
        }

        int maxWidth() {
            int sum = 0;
            for (Node n : items) {
                int w = n.maxWidth();
                if (w < 0) return -1;
                sum += w;
            }
            return sum;
        }
    }

    static final class Alt extends Node {
        final Node[] alts;

        Alt(List<Node> alts) {
            this.alts = alts.toArray(new Node[alts.size()]);
        }

        boolean match(Matcher m, int i, Cont k) {
            for (Node a : alts) {
                if (a.match(m, i, k)) return true;
            }
            return false;
        }

        int maxWidth() {
            int max = 0;
            for (Node n : alts) {
                int w = n.maxWidth();
                if (w < 0) return -1;
                max = Math.max(max, w);
            }
            return max;
        }
    }

    static final class Group extends Node {
        final int n;
        final Node body;

        Group(int n, Node body) {
            this.n = n;
            this.body = body;
        }

        boolean match(final Matcher m, final int i, final Cont k) {
            return body.match(m, i, new Cont() {
                boolean run(int j) {
                    int os = m.groups[2 * n], oe = m.groups[2 * n + 1];
                    m.groups[2 * n] = i;
                    m.groups[2 * n + 1] = j;
                    if (k.run(j)) return true;
                    m.groups[2 * n] = os;
                    m.groups[2 * n + 1] = oe;
                    return false;
                }
            });
        }

        int maxWidth() {
            return body.maxWidth();
        }
    }

    static final int GREEDY = 0, LAZY = 1, POSSESSIVE = 2;

    static final class Repeat extends Node {
        final Node body;
        final int min, max, type;

        Repeat(Node body, int min, int max, int type) {
            this.body = body;
            this.min = min;
            this.max = max;
            this.type = type;
        }

        boolean match(Matcher m, int i, Cont k) {
            if (body instanceof CharNode) return matchChars(m, i, k, ((CharNode) body).pred);
            if (type == POSSESSIVE) {
                final int[] end = new int[1];
                Cont first = new Cont() {
                    boolean run(int j) {
                        end[0] = j;
                        return true;
                    }
                };
                int count = 0, pos = i;
                while (count < max) {
                    if (!body.match(m, pos, first)) break;
                    if (end[0] == pos && count >= min) break;
                    pos = end[0];
                    count++;
                }
                return count >= min && k.run(pos);
            }
            return rep(m, 0, i, k);
        }

        private boolean matchChars(Matcher m, int i, Cont k, CharPred p) {
            int limit = m.to;
            if (type == LAZY) {
                int c = 0;
                while (c < min) {
                    if (i + c >= limit) {
                        m.hitEnd = true;
                        return false;
                    }
                    if (!p.is(m.text.charAt(i + c))) return false;
                    c++;
                }
                for (;;) {
                    if (k.run(i + c)) return true;
                    if (c >= max) return false;
                    if (i + c >= limit) {
                        m.hitEnd = true;
                        return false;
                    }
                    if (!p.is(m.text.charAt(i + c))) return false;
                    c++;
                }
            }
            int n = 0;
            while (n < max && i + n < limit && p.is(m.text.charAt(i + n))) n++;
            if (i + n >= limit) m.hitEnd = true;
            if (n < min) return false;
            if (type == POSSESSIVE) return k.run(i + n);
            for (int c = n; c >= min; c--) {
                if (k.run(i + c)) return true;
            }
            return false;
        }

        boolean rep(final Matcher m, final int count, final int i, final Cont k) {
            Cont again = new Cont() {
                boolean run(int j) {
                    // An iteration that consumed nothing cannot make progress.
                    if (j == i && count >= min) return false;
                    return rep(m, count + 1, j, k);
                }
            };
            if (type == LAZY) {
                if (count >= min && k.run(i)) return true;
                return count < max && body.match(m, i, again);
            }
            if (count < max && body.match(m, i, again)) return true;
            return count >= min && k.run(i);
        }

        int maxWidth() {
            if (max == Integer.MAX_VALUE) return -1;
            int w = body.maxWidth();
            return w < 0 ? -1 : w * max;
        }
    }

    static final class Atomic extends Node {
        final Node body;

        Atomic(Node body) {
            this.body = body;
        }

        boolean match(Matcher m, int i, Cont k) {
            final int[] end = {-1};
            if (!body.match(m, i, new Cont() {
                boolean run(int j) {
                    end[0] = j;
                    return true;
                }
            })) {
                return false;
            }
            return k.run(end[0]);
        }

        int maxWidth() {
            return body.maxWidth();
        }
    }

    static final class Look extends Node {
        final Node body;
        final boolean behind, negative;

        Look(Node body, boolean behind, boolean negative) {
            this.body = body;
            this.behind = behind;
            this.negative = negative;
        }

        boolean match(Matcher m, final int i, Cont k) {
            boolean found;
            if (!behind) {
                found = body.match(m, i, ANY);
            } else {
                Cont endsHere = new Cont() {
                    boolean run(int j) {
                        return j == i;
                    }
                };
                int w = body.maxWidth();
                int lowest = w < 0 ? m.from : Math.max(m.from, i - w);
                int savedTo = m.to;
                m.to = i;
                found = false;
                try {
                    for (int s = i; s >= lowest && !found; s--) found = body.match(m, s, endsHere);
                } finally {
                    m.to = savedTo;
                }
            }
            return found != negative && k.run(i);
        }

        int maxWidth() {
            return 0;
        }
    }

    static final int BEGIN = 0, END = 1, INPUT_BEGIN = 2, INPUT_END = 3, INPUT_END_BEFORE_TERMINATOR = 4,
            WORD_BOUNDARY = 5, NOT_WORD_BOUNDARY = 6, LAST_MATCH = 7;

    static final class Anchor extends Node {
        final int kind;
        final boolean multiline, unixLines;

        Anchor(int kind, int flags) {
            this.kind = kind;
            this.multiline = (flags & MULTILINE) != 0;
            this.unixLines = (flags & UNIX_LINES) != 0;
        }

        private boolean terminator(char c) {
            return unixLines ? c == '\n' : c == '\n' || c == '\r' || c == '\u0085' || c == ' ' || c == ' ';
        }

        /** At the end, or before a final line terminator. */
        private boolean atEndOrFinalTerminator(Matcher m, int i) {
            if (i == m.to) return true;
            if (i + 1 == m.to && terminator(m.text.charAt(i))) return true;
            return !unixLines && i + 2 == m.to && m.text.charAt(i) == '\r' && m.text.charAt(i + 1) == '\n';
        }

        private static boolean word(char c) {
            return c == '_' || Character.isLetterOrDigit(c);
        }

        boolean match(Matcher m, int i, Cont k) {
            boolean ok;
            switch (kind) {
                case BEGIN:
                    if (!multiline) {
                        ok = i == m.from;
                    } else {
                        ok = i == m.from || (i < m.to && i > m.from && terminator(m.text.charAt(i - 1))
                                && !(m.text.charAt(i - 1) == '\r' && m.text.charAt(i) == '\n'));
                    }
                    break;
                case END:
                    if (!multiline) {
                        ok = atEndOrFinalTerminator(m, i);
                    } else {
                        ok = i == m.to || (terminator(m.text.charAt(i))
                                && !(m.text.charAt(i) == '\n' && i > m.from && m.text.charAt(i - 1) == '\r'));
                    }
                    if (i == m.to) m.hitEnd = true;
                    break;
                case INPUT_BEGIN:
                    ok = i == m.from;
                    break;
                case INPUT_END:
                    ok = i == m.to;
                    m.hitEnd = true;
                    break;
                case INPUT_END_BEFORE_TERMINATOR:
                    ok = atEndOrFinalTerminator(m, i);
                    m.hitEnd = true;
                    break;
                case WORD_BOUNDARY:
                case NOT_WORD_BOUNDARY: {
                    boolean before = i > m.from && word(m.text.charAt(i - 1));
                    boolean after = i < m.to && word(m.text.charAt(i));
                    if (i >= m.to) m.hitEnd = true;
                    ok = (before != after) == (kind == WORD_BOUNDARY);
                    break;
                }
                case LAST_MATCH:
                    ok = i == m.lastMatchEnd;
                    break;
                default:
                    ok = false;
            }
            return ok && k.run(i);
        }

        int maxWidth() {
            return 0;
        }
    }

    static final class BackRef extends Node {
        final int n;
        final boolean ci;

        BackRef(int n, boolean ci) {
            this.n = n;
            this.ci = ci;
        }

        boolean match(Matcher m, int i, Cont k) {
            int s = m.groups[2 * n], e = m.groups[2 * n + 1];
            if (s < 0) return false;
            int len = e - s;
            if (i + len > m.to) {
                m.hitEnd = true;
                return false;
            }
            for (int x = 0; x < len; x++) {
                char a = m.text.charAt(s + x), b = m.text.charAt(i + x);
                if (a != b && !(ci && sameIgnoringCase(a, b))) return false;
            }
            return k.run(i + len);
        }
    }

    static boolean sameIgnoringCase(int a, int b) {
        return a == b || Character.toLowerCase(a) == Character.toLowerCase(b)
                || Character.toUpperCase(a) == Character.toUpperCase(b);
    }

    // --- character predicates -----------------------------------------------------------

    static CharPred single(final int c, boolean ci) {
        if (ci && Character.toLowerCase(c) != Character.toUpperCase(c)) {
            return new CharPred() {
                boolean is(int x) {
                    return sameIgnoringCase(x, c);
                }
            };
        }
        return new CharPred() {
            boolean is(int x) {
                return x == c;
            }
        };
    }

    static CharPred range(final int lo, final int hi, final boolean ci) {
        return new CharPred() {
            boolean is(int x) {
                if (x >= lo && x <= hi) return true;
                if (!ci) return false;
                int l = Character.toLowerCase(x), u = Character.toUpperCase(x);
                return (l >= lo && l <= hi) || (u >= lo && u <= hi);
            }
        };
    }

    static CharPred union(final List<CharPred> preds) {
        if (preds.size() == 1) return preds.get(0);
        final CharPred[] all = preds.toArray(new CharPred[preds.size()]);
        return new CharPred() {
            boolean is(int x) {
                for (CharPred p : all) {
                    if (p.is(x)) return true;
                }
                return false;
            }
        };
    }

    static CharPred not(final CharPred p) {
        return new CharPred() {
            boolean is(int x) {
                return !p.is(x);
            }
        };
    }

    static CharPred and(final CharPred a, final CharPred b) {
        return new CharPred() {
            boolean is(int x) {
                return a.is(x) && b.is(x);
            }
        };
    }

    static final int DIGIT = 0, WORD = 1, SPACE = 2, HSPACE = 3, VSPACE = 4, LOWER = 5, UPPER = 6, ASCII = 7,
            ALPHA = 8, ALNUM = 9, PUNCT = 10, GRAPH = 11, PRINT = 12, BLANK = 13, CNTRL = 14, XDIGIT = 15,
            LETTER = 16, UDIGIT = 17, UWHITESPACE = 18, ULOWER = 19, UUPPER = 20, UPUNCT = 21, ANYCHAR = 22;

    static CharPred ctype(final int type) {
        return new CharPred() {
            boolean is(int c) {
                switch (type) {
                    case DIGIT: return c >= '0' && c <= '9';
                    case WORD: return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
                    case SPACE: return c == ' ' || c == '\t' || c == '\n' || c == 0x0b || c == '\f' || c == '\r';
                    case HSPACE: return c == ' ' || c == '\t' || c == 0xa0 || c == 0x1680 || c == 0x180e
                            || (c >= 0x2000 && c <= 0x200a) || c == 0x202f || c == 0x205f || c == 0x3000;
                    case VSPACE: return c == '\n' || c == 0x0b || c == '\f' || c == '\r' || c == 0x85 || c == 0x2028
                            || c == 0x2029;
                    case LOWER: return c >= 'a' && c <= 'z';
                    case UPPER: return c >= 'A' && c <= 'Z';
                    case ASCII: return c < 0x80;
                    case ALPHA: return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
                    case ALNUM: return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
                    case PUNCT: return (c >= 0x21 && c <= 0x2f) || (c >= 0x3a && c <= 0x40) || (c >= 0x5b && c <= 0x60)
                            || (c >= 0x7b && c <= 0x7e);
                    case GRAPH: return c >= 0x21 && c <= 0x7e;
                    case PRINT: return c >= 0x20 && c <= 0x7e;
                    case BLANK: return c == ' ' || c == '\t';
                    case CNTRL: return c < 0x20 || c == 0x7f;
                    case XDIGIT: return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
                    case LETTER: return Character.isLetter(c);
                    case UDIGIT: return Character.isDigit(c);
                    case UWHITESPACE: return Character.isWhitespace(c) || Character.isSpaceChar(c);
                    case ULOWER: return Character.isLowerCase(c);
                    case UUPPER: return Character.isUpperCase(c);
                    case UPUNCT: return c < 0x80 ? (c >= 0x21 && c <= 0x2f) || (c >= 0x3a && c <= 0x40)
                            || (c >= 0x5b && c <= 0x60) || (c >= 0x7b && c <= 0x7e)
                            : !Character.isLetterOrDigit(c) && !Character.isWhitespace(c) && !Character.isISOControl(c);
                    default: return true;
                }
            }
        };
    }

    // --- parser ---------------------------------------------------------------------

    static final class Parser {
        final String p;
        final int len;
        int i;
        int flags;
        int groups;
        final Map<String, Integer> names = new HashMap<String, Integer>();

        Parser(String p, int flags) {
            this.p = p;
            this.len = p.length();
            this.flags = flags;
        }

        PatternSyntaxException error(String desc) {
            return new PatternSyntaxException(desc, p, i);
        }

        boolean ci() {
            return (flags & CASE_INSENSITIVE) != 0;
        }

        Node parseAll() {
            if ((flags & LITERAL) != 0) {
                ArrayList<Node> items = new ArrayList<Node>();
                for (int k = 0; k < len; k++) items.add(new CharNode(single(p.charAt(k), ci())));
                return items.size() == 1 ? items.get(0) : new Seq(items);
            }
            Node n = parseAlt();
            if (i < len) throw error("Unmatched closing ')'");
            return n;
        }

        void skipComments() {
            if ((flags & COMMENTS) == 0) return;
            while (i < len) {
                char c = p.charAt(i);
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == 0x0b) {
                    i++;
                } else if (c == '#') {
                    while (i < len && p.charAt(i) != '\n') i++;
                } else {
                    break;
                }
            }
        }

        Node parseAlt() {
            ArrayList<Node> alts = new ArrayList<Node>();
            alts.add(parseSeq());
            while (i < len && p.charAt(i) == '|') {
                i++;
                alts.add(parseSeq());
            }
            return alts.size() == 1 ? alts.get(0) : new Alt(alts);
        }

        Node parseSeq() {
            ArrayList<Node> items = new ArrayList<Node>();
            for (;;) {
                skipComments();
                if (i >= len) break;
                char c = p.charAt(i);
                if (c == '|' || c == ')') break;
                Node atom = parseAtom();
                if (atom == null) continue;
                items.add(parseQuantifier(atom));
            }
            if (items.isEmpty()) return new Empty();
            return items.size() == 1 ? items.get(0) : new Seq(items);
        }

        Node parseQuantifier(Node atom) {
            skipComments();
            if (i >= len) return atom;
            char c = p.charAt(i);
            int min, max;
            if (c == '*') {
                min = 0;
                max = Integer.MAX_VALUE;
                i++;
            } else if (c == '+') {
                min = 1;
                max = Integer.MAX_VALUE;
                i++;
            } else if (c == '?') {
                min = 0;
                max = 1;
                i++;
            } else if (c == '{' && isInterval(i)) {
                i++;
                min = number();
                max = min;
                if (p.charAt(i) == ',') {
                    i++;
                    max = p.charAt(i) == '}' ? Integer.MAX_VALUE : number();
                }
                i++;  // '}'
                if (max < min) throw error("Illegal repetition range");
            } else {
                return atom;
            }
            int type = GREEDY;
            if (i < len && p.charAt(i) == '?') {
                type = LAZY;
                i++;
            } else if (i < len && p.charAt(i) == '+') {
                type = POSSESSIVE;
                i++;
            }
            if (atom instanceof Anchor || atom instanceof Look) return atom;
            return parseQuantifier(new Repeat(atom, min, max, type));
        }

        boolean isInterval(int at) {
            int k = at + 1;
            int digits = 0;
            while (k < len && Character.isDigit(p.charAt(k))) {
                k++;
                digits++;
            }
            if (digits == 0) return false;
            if (k < len && p.charAt(k) == ',') {
                k++;
                while (k < len && Character.isDigit(p.charAt(k))) k++;
            }
            return k < len && p.charAt(k) == '}';
        }

        int number() {
            int start = i;
            while (i < len && Character.isDigit(p.charAt(i))) i++;
            if (start == i) throw error("Illegal repetition");
            try {
                return Integer.parseInt(p.substring(start, i));
            } catch (NumberFormatException e) {
                throw error("Illegal repetition range");
            }
        }

        Node parseAtom() {
            char c = p.charAt(i++);
            switch (c) {
                case '(':
                    return parseGroup();
                case '[':
                    return new CharNode(parseClass());
                case '.': {
                    if ((flags & DOTALL) != 0) return new CharNode(ctype(ANYCHAR));
                    if ((flags & UNIX_LINES) != 0) return new CharNode(not(single('\n', false)));
                    return new CharNode(new CharPred() {
                        boolean is(int x) {
                            return x != '\n' && x != '\r' && x != '\u0085' && x != ' ' && x != ' ';
                        }
                    });
                }
                case '^':
                    return new Anchor(BEGIN, flags);
                case '$':
                    return new Anchor(END, flags);
                case '\\':
                    return parseEscapeNode();
                case '*':
                case '+':
                case '?':
                    i--;
                    throw error("Dangling meta character '" + c + "'");
                case '{':
                    if (isInterval(i - 1)) {
                        i--;
                        throw error("Dangling meta character '{'");
                    }
                    return new CharNode(single('{', false));
                default:
                    return new CharNode(single(c, ci()));
            }
        }

        Node parseGroup() {
            int savedFlags = flags;
            Node result;
            if (i < len && p.charAt(i) == '?') {
                i++;
                if (i >= len) throw error("Unknown group type");
                char t = p.charAt(i++);
                switch (t) {
                    case ':':
                        result = parseAlt();
                        break;
                    case '=':
                        result = new Look(parseAlt(), false, false);
                        break;
                    case '!':
                        result = new Look(parseAlt(), false, true);
                        break;
                    case '>':
                        result = new Atomic(parseAlt());
                        break;
                    case '<': {
                        if (i < len && (p.charAt(i) == '=' || p.charAt(i) == '!')) {
                            boolean negative = p.charAt(i++) == '!';
                            result = new Look(parseAlt(), true, negative);
                        } else {
                            int end = p.indexOf('>', i);
                            if (end < 0) throw error("named capturing group is missing trailing '>'");
                            String name = p.substring(i, end);
                            if (name.isEmpty() || !Character.isLetter(name.charAt(0))) {
                                throw error("capturing group name does not start with a Latin letter");
                            }
                            if (names.containsKey(name)) throw error("Named capturing group <" + name + "> is already defined");
                            i = end + 1;
                            int n = ++groups;
                            names.put(name, n);
                            result = new Group(n, parseAlt());
                        }
                        break;
                    }
                    default: {
                        i--;
                        // Inline flags: (?idmsux-idmsux) or (?idmsux-idmsux:X)
                        boolean on = true;
                        for (;;) {
                            if (i >= len) throw error("Unknown inline modifier");
                            char f = p.charAt(i++);
                            if (f == '-') {
                                on = false;
                                continue;
                            }
                            if (f == ')') {
                                return null;  // applies to the rest of the enclosing group
                            }
                            if (f == ':') break;
                            int bit;
                            switch (f) {
                                case 'i': bit = CASE_INSENSITIVE; break;
                                case 'd': bit = UNIX_LINES; break;
                                case 'm': bit = MULTILINE; break;
                                case 's': bit = DOTALL; break;
                                case 'u': bit = UNICODE_CASE; break;
                                case 'x': bit = COMMENTS; break;
                                case 'U': bit = UNICODE_CHARACTER_CLASS; break;
                                default:
                                    i--;
                                    throw error("Unknown inline modifier");
                            }
                            flags = on ? flags | bit : flags & ~bit;
                        }
                        result = parseAlt();
                        break;
                    }
                }
            } else {
                int n = ++groups;
                result = new Group(n, parseAlt());
            }
            if (i >= len || p.charAt(i) != ')') throw error("Unclosed group");
            i++;
            flags = savedFlags;
            return result;
        }

        Node parseEscapeNode() {
            if (i >= len) throw error("Unexpected internal error");
            char c = p.charAt(i);
            switch (c) {
                case 'b':
                    i++;
                    return new Anchor(WORD_BOUNDARY, flags);
                case 'B':
                    i++;
                    return new Anchor(NOT_WORD_BOUNDARY, flags);
                case 'A':
                    i++;
                    return new Anchor(INPUT_BEGIN, flags);
                case 'z':
                    i++;
                    return new Anchor(INPUT_END, flags);
                case 'Z':
                    i++;
                    return new Anchor(INPUT_END_BEFORE_TERMINATOR, flags);
                case 'G':
                    i++;
                    return new Anchor(LAST_MATCH, flags);
                case 'Q': {
                    i++;
                    int end = p.indexOf("\\E", i);
                    String quoted = end < 0 ? p.substring(i) : p.substring(i, end);
                    i = end < 0 ? len : end + 2;
                    ArrayList<Node> items = new ArrayList<Node>();
                    for (int k = 0; k < quoted.length(); k++) items.add(new CharNode(single(quoted.charAt(k), ci())));
                    if (items.isEmpty()) return null;
                    return items.size() == 1 ? items.get(0) : new Seq(items);
                }
                case 'k': {
                    i++;
                    if (i >= len || p.charAt(i) != '<') throw error("\\k is not followed by '<' for named capturing group");
                    int end = p.indexOf('>', i);
                    if (end < 0) throw error("named capturing group is missing trailing '>'");
                    String name = p.substring(i + 1, end);
                    Integer n = names.get(name);
                    if (n == null) throw error("named capturing group <" + name + "> does not exist");
                    i = end + 1;
                    return new BackRef(n, ci());
                }
                case 'R': {
                    i++;
                    ArrayList<Node> crlf = new ArrayList<Node>();
                    crlf.add(new CharNode(single('\r', false)));
                    crlf.add(new CharNode(single('\n', false)));
                    ArrayList<Node> alts = new ArrayList<Node>();
                    alts.add(new Seq(crlf));
                    alts.add(new CharNode(ctype(VSPACE)));
                    return new Atomic(new Alt(alts));
                }
                default:
                    break;
            }
            if (c >= '1' && c <= '9') {
                int n = c - '0';
                i++;
                while (i < len && Character.isDigit(p.charAt(i))) {
                    int next = n * 10 + (p.charAt(i) - '0');
                    if (next > groups) break;
                    n = next;
                    i++;
                }
                return new BackRef(n, ci());
            }
            return new CharNode(parseEscapePred());
        }

        /** An escape that stands for characters (after the backslash). */
        CharPred parseEscapePred() {
            char c = p.charAt(i++);
            switch (c) {
                case 'd': return ctype(unicodeClasses() ? UDIGIT : DIGIT);
                case 'D': return not(ctype(unicodeClasses() ? UDIGIT : DIGIT));
                case 'w': return unicodeClasses() ? unicodeWord() : ctype(WORD);
                case 'W': return not(unicodeClasses() ? unicodeWord() : ctype(WORD));
                case 's': return ctype(unicodeClasses() ? UWHITESPACE : SPACE);
                case 'S': return not(ctype(unicodeClasses() ? UWHITESPACE : SPACE));
                case 'h': return ctype(HSPACE);
                case 'H': return not(ctype(HSPACE));
                case 'v': return ctype(VSPACE);
                case 'V': return not(ctype(VSPACE));
                case 't': return single('\t', false);
                case 'n': return single('\n', false);
                case 'r': return single('\r', false);
                case 'f': return single('\f', false);
                case 'a': return single(0x07, false);
                case 'e': return single(0x1b, false);
                case '0': {
                    int v = 0, digits = 0;
                    while (i < len && digits < 3 && p.charAt(i) >= '0' && p.charAt(i) <= '7') {
                        int next = v * 8 + (p.charAt(i) - '0');
                        if (next > 0377) break;
                        v = next;
                        i++;
                        digits++;
                    }
                    if (digits == 0) throw error("Illegal octal escape sequence");
                    return single(v, ci());
                }
                case 'x': {
                    if (i < len && p.charAt(i) == '{') {
                        int end = p.indexOf('}', i);
                        if (end < 0) throw error("Unclosed hexadecimal escape sequence");
                        int v = hex(i + 1, end);
                        i = end + 1;
                        return single(v, ci());
                    }
                    int v = hex(i, i + 2);
                    i += 2;
                    return single(v, ci());
                }
                case 'u': {
                    int v = hex(i, i + 4);
                    i += 4;
                    return single(v, ci());
                }
                case 'c': {
                    if (i >= len) throw error("Illegal control escape sequence");
                    return single(p.charAt(i++) ^ 64, false);
                }
                case 'p':
                case 'P': {
                    String name;
                    if (i < len && p.charAt(i) == '{') {
                        int end = p.indexOf('}', i);
                        if (end < 0) throw error("Unclosed character family");
                        name = p.substring(i + 1, end);
                        i = end + 1;
                    } else {
                        if (i >= len) throw error("Illegal character family");
                        name = String.valueOf(p.charAt(i++));
                    }
                    CharPred pred = property(name);
                    return c == 'P' ? not(pred) : pred;
                }
                default:
                    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                        i--;
                        throw error("Illegal/unsupported escape sequence");
                    }
                    return single(c, ci());
            }
        }

        boolean unicodeClasses() {
            return (flags & UNICODE_CHARACTER_CLASS) != 0;
        }

        CharPred unicodeWord() {
            return new CharPred() {
                boolean is(int x) {
                    return x == '_' || Character.isLetterOrDigit(x);
                }
            };
        }

        int hex(int from, int to) {
            if (to > len || from >= to) throw error("Illegal hexadecimal escape sequence");
            try {
                return Integer.parseInt(p.substring(from, to), 16);
            } catch (NumberFormatException e) {
                throw error("Illegal hexadecimal escape sequence");
            }
        }

        CharPred property(String name) {
            String n = name;
            if (n.startsWith("Is")) n = n.substring(2);
            else if (n.startsWith("general_category=")) n = n.substring(17);
            else if (n.startsWith("gc=")) n = n.substring(3);
            switch (n) {
                case "Lower": return unicodeClasses() ? ctype(ULOWER) : ctype(LOWER);
                case "Upper": return unicodeClasses() ? ctype(UUPPER) : ctype(UPPER);
                case "ASCII": return ctype(ASCII);
                case "Alpha": return unicodeClasses() ? ctype(LETTER) : ctype(ALPHA);
                case "Digit": return unicodeClasses() ? ctype(UDIGIT) : ctype(DIGIT);
                case "Alnum": return unicodeClasses() ? unicodeWordNoUnderscore() : ctype(ALNUM);
                case "Punct": return unicodeClasses() ? ctype(UPUNCT) : ctype(PUNCT);
                case "Graph": return ctype(GRAPH);
                case "Print": return ctype(PRINT);
                case "Blank": return ctype(BLANK);
                case "Cntrl": return ctype(CNTRL);
                case "XDigit": return ctype(XDIGIT);
                case "Space": return unicodeClasses() ? ctype(UWHITESPACE) : ctype(SPACE);
                case "javaLowerCase":
                case "Lowercase":
                case "Ll": return ctype(ULOWER);
                case "javaUpperCase":
                case "Uppercase":
                case "Lu": return ctype(UUPPER);
                case "javaWhitespace":
                case "White_Space":
                case "WhiteSpace":
                case "Z":
                case "Zs": return ctype(UWHITESPACE);
                case "javaLetter":
                case "Letter":
                case "Alphabetic":
                case "L": return ctype(LETTER);
                case "javaDigit":
                case "N":
                case "Nd": return ctype(UDIGIT);
                case "javaLetterOrDigit": return unicodeWordNoUnderscore();
                case "P":
                case "Punctuation": return ctype(UPUNCT);
                case "InBasicLatin":
                case "InBasic_Latin": return range(0, 0x7f, false);
                case "InLatin-1Supplement":
                case "InLatin_1_Supplement": return range(0x80, 0xff, false);
                case "InGreek": return range(0x370, 0x3ff, false);
                case "InCyrillic": return range(0x400, 0x4ff, false);
                case "InCJKUnifiedIdeographs":
                case "InCJK_Unified_Ideographs": return range(0x4e00, 0x9fff, false);
                case "InHiragana": return range(0x3040, 0x309f, false);
                case "InKatakana": return range(0x30a0, 0x30ff, false);
                case "InHangulSyllables": return range(0xac00, 0xd7af, false);
                default:
                    throw error("Unknown character property name {" + name + "}");
            }
        }

        CharPred unicodeWordNoUnderscore() {
            return new CharPred() {
                boolean is(int x) {
                    return Character.isLetterOrDigit(x);
                }
            };
        }

        /** After '['. */
        CharPred parseClass() {
            boolean negate = false;
            if (i < len && p.charAt(i) == '^') {
                negate = true;
                i++;
            }
            ArrayList<CharPred> union = new ArrayList<CharPred>();
            CharPred intersection = null;
            boolean first = true;
            for (;;) {
                if (i >= len) throw error("Unclosed character class");
                char c = p.charAt(i);
                if (c == ']' && !first) {
                    i++;
                    break;
                }
                first = false;
                if (c == '[') {
                    i++;
                    union.add(parseClass());
                    continue;
                }
                if (c == '&' && i + 1 < len && p.charAt(i + 1) == '&') {
                    i += 2;
                    CharPred left = union.isEmpty() ? null : union(new ArrayList<CharPred>(union));
                    intersection = intersection == null ? left : (left == null ? intersection : and(intersection, left));
                    union.clear();
                    continue;
                }
                if ((flags & COMMENTS) != 0 && (c == ' ' || c == '\t' || c == '\n' || c == '\r')) {
                    i++;
                    continue;
                }
                int lo;
                CharPred escaped = null;
                if (c == '\\') {
                    i++;
                    if (i >= len) throw error("Unclosed character class");
                    char e = p.charAt(i);
                    if (e == 'Q') {
                        i++;
                        int end = p.indexOf("\\E", i);
                        String quoted = end < 0 ? p.substring(i) : p.substring(i, end);
                        i = end < 0 ? len : end + 2;
                        for (int k = 0; k < quoted.length(); k++) union.add(single(quoted.charAt(k), ci()));
                        continue;
                    }
                    int before = i;
                    escaped = parseEscapePred();
                    lo = singleValue(before);
                    if (lo < 0) {
                        union.add(escaped);
                        continue;
                    }
                } else {
                    i++;
                    lo = c;
                }
                // A range lo-hi, unless '-' is last or followed by a class.
                if (i + 1 < len && p.charAt(i) == '-' && p.charAt(i + 1) != ']' && p.charAt(i + 1) != '[') {
                    i++;
                    int hi;
                    char h = p.charAt(i);
                    if (h == '\\') {
                        i++;
                        int before = i;
                        parseEscapePred();
                        hi = singleValue(before);
                        if (hi < 0) throw error("Illegal character range");
                    } else {
                        i++;
                        hi = h;
                    }
                    if (hi < lo) throw error("Illegal character range");
                    union.add(range(lo, hi, ci()));
                } else {
                    union.add(escaped != null ? escaped : single(lo, ci()));
                }
            }
            CharPred result = union.isEmpty() ? null : union(union);
            if (intersection != null) result = result == null ? intersection : and(intersection, result);
            if (result == null) {
                result = new CharPred() {
                    boolean is(int x) {
                        return false;
                    }
                };
            }
            return negate ? not(result) : result;
        }

        /** The character a just-parsed escape (starting at index from) denotes, or -1 for a class. */
        int singleValue(int from) {
            char e = p.charAt(from);
            switch (e) {
                case 't': return '\t';
                case 'n': return '\n';
                case 'r': return '\r';
                case 'f': return '\f';
                case 'a': return 0x07;
                case 'e': return 0x1b;
                case 'c': return p.charAt(from + 1) ^ 64;
                case '0': return Integer.parseInt(p.substring(from + 1, i), 8);
                case 'x':
                    if (p.charAt(from + 1) == '{') return Integer.parseInt(p.substring(from + 2, i - 1), 16);
                    return Integer.parseInt(p.substring(from + 1, i), 16);
                case 'u': return Integer.parseInt(p.substring(from + 1, i), 16);
                default:
                    if ((e >= 'a' && e <= 'z') || (e >= 'A' && e <= 'Z')) return -1;
                    return e;
            }
        }
    }
}
