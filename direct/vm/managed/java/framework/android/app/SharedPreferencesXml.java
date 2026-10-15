package android.app;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

/**
 * Reads and writes the XML map format Android's XmlUtils uses for shared
 * preferences, so files written here and on a device are interchangeable:
 *
 * <pre>
 * &lt;?xml version='1.0' encoding='utf-8' standalone='yes' ?&gt;
 * &lt;map&gt;
 *     &lt;int name="taps" value="3" /&gt;
 *     &lt;string name="k"&gt;text&lt;/string&gt;
 * &lt;/map&gt;
 * </pre>
 */
final class SharedPreferencesXml {
    private SharedPreferencesXml() {}

    // --- writing ------------------------------------------------------------------

    static String write(Map<String, Object> map) {
        StringBuilder out = new StringBuilder(256);
        out.append("<?xml version='1.0' encoding='utf-8' standalone='yes' ?>\n<map>\n");
        for (Map.Entry<String, Object> e : map.entrySet()) {
            writeValue(out, e.getKey(), e.getValue());
        }
        out.append("</map>\n");
        return out.toString();
    }

    private static void writeValue(StringBuilder out, String name, Object v) {
        out.append("    ");
        if (v == null) {
            out.append("<null name=\"");
            escape(out, name);
            out.append("\" />\n");
        } else if (v instanceof String) {
            out.append("<string name=\"");
            escape(out, name);
            out.append("\">");
            escape(out, (String) v);
            out.append("</string>\n");
        } else if (v instanceof Set) {
            out.append("<set name=\"");
            escape(out, name);
            out.append("\">\n");
            for (Object s : (Set<?>) v) {
                if (s == null) {
                    out.append("        <null />\n");
                } else {
                    out.append("        <string>");
                    escape(out, s.toString());
                    out.append("</string>\n");
                }
            }
            out.append("    </set>\n");
        } else {
            String type;
            if (v instanceof Integer) type = "int";
            else if (v instanceof Long) type = "long";
            else if (v instanceof Float) type = "float";
            else if (v instanceof Boolean) type = "boolean";
            else throw new IllegalArgumentException("writeValue: unable to write value " + v);
            out.append('<').append(type).append(" name=\"");
            escape(out, name);
            out.append("\" value=\"").append(v.toString()).append("\" />\n");
        }
    }

    private static void escape(StringBuilder out, String s) {
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            switch (c) {
                case '&':
                    out.append("&amp;");
                    break;
                case '<':
                    out.append("&lt;");
                    break;
                case '>':
                    out.append("&gt;");
                    break;
                case '"':
                    out.append("&quot;");
                    break;
                default:
                    if (c < 0x20) {
                        out.append("&#").append((int) c).append(';');
                    } else {
                        out.append(c);
                    }
            }
        }
    }

    // --- reading ------------------------------------------------------------------

    private static final class Element {
        final String name;
        final HashMap<String, String> attrs = new HashMap<String, String>();
        final ArrayList<Element> children = new ArrayList<Element>();
        final StringBuilder text = new StringBuilder();

        Element(String name) {
            this.name = name;
        }
    }

    static HashMap<String, Object> read(String xml) throws IOException {
        Parser p = new Parser(xml);
        Element root = p.document();
        if (!root.name.equals("map")) throw new IOException("Expected map, got <" + root.name + ">");
        HashMap<String, Object> map = new HashMap<String, Object>();
        for (Element e : root.children) {
            String name = e.attrs.get("name");
            if (name == null) throw new IOException("Missing name attribute in <" + e.name + ">");
            map.put(name, value(e));
        }
        return map;
    }

    private static Object value(Element e) throws IOException {
        String v = e.attrs.get("value");
        try {
            if (e.name.equals("string")) return e.text.toString();
            if (e.name.equals("null")) return null;
            if (e.name.equals("int")) return Integer.valueOf(Integer.parseInt(required(e, v)));
            if (e.name.equals("long")) return Long.valueOf(Long.parseLong(required(e, v)));
            if (e.name.equals("float")) return Float.valueOf(Float.parseFloat(required(e, v)));
            if (e.name.equals("boolean")) return Boolean.valueOf(required(e, v));
            if (e.name.equals("set")) {
                HashSet<String> set = new HashSet<String>();
                for (Element c : e.children) {
                    if (c.name.equals("string")) set.add(c.text.toString());
                    else if (c.name.equals("null")) set.add(null);
                    else throw new IOException("Unexpected <" + c.name + "> in set");
                }
                return set;
            }
        } catch (NumberFormatException ex) {
            throw new IOException("Bad value in <" + e.name + ">: " + v);
        }
        throw new IOException("Unknown tag: " + e.name);
    }

    private static String required(Element e, String v) throws IOException {
        if (v == null) throw new IOException("Need value attribute in <" + e.name + ">");
        return v;
    }

    /** A strict parser for the subset of XML the preferences format needs. */
    private static final class Parser {
        private final String s;
        private int pos;

        Parser(String s) {
            this.s = s;
        }

        Element document() throws IOException {
            skipMisc();
            if (pos < s.length() && s.startsWith("<?", pos)) {
                int end = s.indexOf("?>", pos);
                if (end < 0) throw error("unterminated XML declaration");
                pos = end + 2;
            }
            skipMisc();
            Element root = element();
            skipMisc();
            if (pos != s.length()) throw error("content after the root element");
            return root;
        }

        private IOException error(String what) {
            return new IOException(what + " at offset " + pos);
        }

        private void skipMisc() throws IOException {
            for (;;) {
                while (pos < s.length() && isSpace(s.charAt(pos))) pos++;
                if (s.startsWith("<!--", pos)) {
                    int end = s.indexOf("-->", pos + 4);
                    if (end < 0) throw error("unterminated comment");
                    pos = end + 3;
                } else {
                    return;
                }
            }
        }

        private static boolean isSpace(char c) {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r';
        }

        private String name() throws IOException {
            int start = pos;
            while (pos < s.length()) {
                char c = s.charAt(pos);
                if (isSpace(c) || c == '=' || c == '>' || c == '/' || c == '<' || c == '"' || c == '\'') break;
                pos++;
            }
            if (pos == start) throw error("expected a name");
            return s.substring(start, pos);
        }

        private void expect(char c) throws IOException {
            if (pos >= s.length() || s.charAt(pos) != c) throw error("expected '" + c + "'");
            pos++;
        }

        private Element element() throws IOException {
            expect('<');
            Element e = new Element(name());
            for (;;) {
                while (pos < s.length() && isSpace(s.charAt(pos))) pos++;
                if (pos >= s.length()) throw error("unterminated start tag");
                char c = s.charAt(pos);
                if (c == '/') {
                    pos++;
                    expect('>');
                    return e;
                }
                if (c == '>') {
                    pos++;
                    break;
                }
                String attr = name();
                while (pos < s.length() && isSpace(s.charAt(pos))) pos++;
                expect('=');
                while (pos < s.length() && isSpace(s.charAt(pos))) pos++;
                if (pos >= s.length()) throw error("unterminated attribute");
                char quote = s.charAt(pos);
                if (quote != '"' && quote != '\'') throw error("expected a quoted attribute value");
                pos++;
                int end = s.indexOf(quote, pos);
                if (end < 0) throw error("unterminated attribute value");
                e.attrs.put(attr, decode(s.substring(pos, end)));
                pos = end + 1;
            }
            for (;;) {
                if (pos >= s.length()) throw error("unterminated element <" + e.name + ">");
                if (s.startsWith("</", pos)) {
                    pos += 2;
                    String closing = name();
                    if (!closing.equals(e.name)) throw error("mismatched </" + closing + "> for <" + e.name + ">");
                    while (pos < s.length() && isSpace(s.charAt(pos))) pos++;
                    expect('>');
                    return e;
                }
                if (s.startsWith("<!--", pos)) {
                    int end = s.indexOf("-->", pos + 4);
                    if (end < 0) throw error("unterminated comment");
                    pos = end + 3;
                } else if (s.startsWith("<![CDATA[", pos)) {
                    int end = s.indexOf("]]>", pos + 9);
                    if (end < 0) throw error("unterminated CDATA");
                    e.text.append(s, pos + 9, end);
                    pos = end + 3;
                } else if (s.charAt(pos) == '<') {
                    e.children.add(element());
                } else {
                    int end = s.indexOf('<', pos);
                    if (end < 0) end = s.length();
                    e.text.append(decode(s.substring(pos, end)));
                    pos = end;
                }
            }
        }

        private String decode(String raw) throws IOException {
            int amp = raw.indexOf('&');
            if (amp < 0) return raw;
            StringBuilder out = new StringBuilder(raw.length());
            int i = 0;
            while (amp >= 0) {
                out.append(raw, i, amp);
                int semi = raw.indexOf(';', amp);
                if (semi < 0) throw error("unterminated entity");
                String ent = raw.substring(amp + 1, semi);
                if (ent.equals("amp")) out.append('&');
                else if (ent.equals("lt")) out.append('<');
                else if (ent.equals("gt")) out.append('>');
                else if (ent.equals("quot")) out.append('"');
                else if (ent.equals("apos")) out.append('\'');
                else if (ent.startsWith("#x") || ent.startsWith("#X")) out.appendCodePoint(codePoint(ent.substring(2), 16));
                else if (ent.startsWith("#")) out.appendCodePoint(codePoint(ent.substring(1), 10));
                else throw error("unknown entity &" + ent + ";");
                i = semi + 1;
                amp = raw.indexOf('&', i);
            }
            out.append(raw, i, raw.length());
            return out.toString();
        }

        private int codePoint(String digits, int radix) throws IOException {
            try {
                int cp = Integer.parseInt(digits, radix);
                if (cp < 0 || cp > 0x10FFFF) throw error("character reference out of range");
                return cp;
            } catch (NumberFormatException e) {
                throw error("bad character reference");
            }
        }
    }
}
