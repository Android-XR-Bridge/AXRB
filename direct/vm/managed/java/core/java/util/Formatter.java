package java.util;

/**
 * printf-style formatting. Supports the general, character, integral and
 * floating-point conversions (b B h H s S c C d o x X e E f g G a A % n) with
 * flags, width, precision and explicit/relative argument indices.
 *
 * Not supported, and rejected with UnsupportedOperationException rather than
 * formatted approximately: date/time conversions (%t / %T), BigInteger and
 * BigDecimal arguments, and Formattable.
 *
 * Floating-point output follows java.util.Formatter: the shortest decimal
 * representation of the value (Double.toString's digits) rounded HALF_UP to
 * the requested precision.
 */
public final class Formatter implements java.io.Closeable, java.io.Flushable {
    private final StringBuilder out;
    private final Locale locale;

    public Formatter() {
        this(new StringBuilder(), Locale.getDefault());
    }

    public Formatter(Locale l) {
        this(new StringBuilder(), l);
    }

    public Formatter(StringBuilder sb) {
        this(sb, Locale.getDefault());
    }

    public Formatter(StringBuilder sb, Locale l) {
        this.out = sb != null ? sb : new StringBuilder();
        this.locale = l;
    }

    public Locale locale() {
        return locale;
    }

    public StringBuilder out() {
        return out;
    }

    @Override
    public String toString() {
        return out.toString();
    }

    public void flush() {}

    public void close() {}

    private static final String FLAGS = "-#+ 0,(<";

    public Formatter format(String format, Object... args) {
        return format(locale, format, args);
    }

    public Formatter format(Locale l, String format, Object... args) {
        int ordinary = 0;      // next implicit argument
        int last = -1;         // index of the last argument used, for '<'
        int i = 0, n = format.length();
        while (i < n) {
            char c = format.charAt(i);
            if (c != '%') {
                out.append(c);
                i++;
                continue;
            }
            int start = i++;
            if (i >= n) throw new UnknownFormatConversionException("Conversion = '%'");
            // [argument_index$][flags][width][.precision]conversion
            int argIndex = -1;
            int j = i;
            while (j < n && Character.isDigit(format.charAt(j))) j++;
            if (j < n && j > i && format.charAt(j) == '$') {
                argIndex = Integer.parseInt(format.substring(i, j));
                if (argIndex < 1) throw new IllegalFormatException("Illegal format argument index = " + argIndex);
                i = j + 1;
            }
            String flags = "";
            while (i < n && FLAGS.indexOf(format.charAt(i)) >= 0) {
                char f = format.charAt(i);
                // '0' directly after flags would be the width; only a leading 0 is a flag.
                if (flags.indexOf(f) >= 0) throw new IllegalFormatFlagsException("Flags = '" + flags + f + "'");
                flags += f;
                i++;
            }
            int width = -1;
            j = i;
            while (j < n && Character.isDigit(format.charAt(j))) j++;
            if (j > i) {
                width = Integer.parseInt(format.substring(i, j));
                i = j;
            }
            int precision = -1;
            if (i < n && format.charAt(i) == '.') {
                j = ++i;
                while (j < n && Character.isDigit(format.charAt(j))) j++;
                if (j == i) throw new IllegalFormatPrecisionException("Precision missing in " + format.substring(start));
                precision = Integer.parseInt(format.substring(i, j));
                i = j;
            }
            if (i >= n) throw new UnknownFormatConversionException("Conversion = '" + format.substring(start) + "'");
            char conv = format.charAt(i++);
            if (conv == 't' || conv == 'T') {
                throw new UnsupportedOperationException("date/time format conversions are not supported: "
                        + format.substring(start, Math.min(i + 1, n)));
            }
            if (conv == 'n') {
                out.append('\n');
                continue;
            }
            if (conv == '%') {
                if (precision >= 0) throw new IllegalFormatPrecisionException(String.valueOf(precision));
                pad(l, "%", flags, width, false);
                continue;
            }
            if ("bBhHsScCdoxXeEfgGaA".indexOf(conv) < 0) {
                throw new UnknownFormatConversionException("Conversion = '" + conv + "'");
            }
            if (flags.indexOf('-') >= 0 && width < 0) {
                throw new MissingFormatWidthException(format.substring(start, i));
            }
            Object arg;
            int index;
            if (flags.indexOf('<') >= 0) {
                index = last;
            } else if (argIndex > 0) {
                index = argIndex - 1;
            } else {
                index = ordinary++;
            }
            if (index < 0 || args == null || index >= args.length) {
                throw new MissingFormatArgumentException("Format specifier '" + format.substring(start, i) + "'");
            }
            last = index;
            arg = args[index];
            flags = flags.replace("<", "");
            formatOne(l, conv, flags, width, precision, arg);
        }
        return this;
    }

    private void formatOne(Locale l, char conv, String flags, int width, int precision, Object arg) {
        boolean upper = Character.isUpperCase(conv);
        switch (Character.toLowerCase(conv)) {
            case 'b': {
                String s = arg == null ? "false" : arg instanceof Boolean ? arg.toString() : "true";
                general(l, s, flags, width, precision, upper);
                return;
            }
            case 'h': {
                String s = arg == null ? "null" : Integer.toHexString(arg.hashCode());
                general(l, s, flags, width, precision, upper);
                return;
            }
            case 's':
                if (flags.indexOf('#') >= 0) throw new FormatFlagsConversionMismatchException("Conversion = s, Flags = #");
                general(l, String.valueOf(arg), flags, width, precision, upper);
                return;
            case 'c': {
                if (precision >= 0) throw new IllegalFormatPrecisionException(String.valueOf(precision));
                String s;
                if (arg == null) {
                    s = "null";
                } else if (arg instanceof Character) {
                    s = String.valueOf(((Character) arg).charValue());
                } else if (arg instanceof Byte || arg instanceof Short || arg instanceof Integer) {
                    int cp = ((Number) arg).intValue();
                    if (!Character.isValidCodePoint(cp)) throw new IllegalFormatException("Code point = 0x" + Integer.toHexString(cp));
                    s = new String(Character.toChars(cp));
                } else {
                    throw badArg(conv, arg);
                }
                pad(l, upper ? s.toUpperCase() : s, flags, width, false);
                return;
            }
            case 'd': case 'o': case 'x':
                integral(l, Character.toLowerCase(conv), upper, flags, width, precision, arg);
                return;
            default:
                floating(l, Character.toLowerCase(conv), upper, flags, width, precision, arg);
        }
    }

    private static IllegalFormatConversionException badArg(char conv, Object arg) {
        return new IllegalFormatConversionException(conv + " != " + arg.getClass().getName());
    }

    private void general(Locale l, String s, String flags, int width, int precision, boolean upper) {
        if (precision >= 0 && precision < s.length()) s = s.substring(0, precision);
        if (upper) s = s.toUpperCase();
        pad(l, s, flags, width, false);
    }

    private void pad(Locale l, String s, String flags, int width, boolean zeroPadAllowed) {
        if (width <= s.length()) {
            out.append(s);
            return;
        }
        int fill = width - s.length();
        if (flags.indexOf('-') >= 0) {
            out.append(s);
            for (int k = 0; k < fill; k++) out.append(' ');
        } else {
            for (int k = 0; k < fill; k++) out.append(' ');
            out.append(s);
        }
    }

    private void integral(Locale l, char conv, boolean upper, String flags, int width, int precision, Object arg) {
        if (precision >= 0) throw new IllegalFormatPrecisionException(String.valueOf(precision));
        if (arg == null) {
            pad(l, "null", flags, width, false);
            return;
        }
        long v;
        int bits;
        if (arg instanceof Integer) {
            v = (Integer) arg;
            bits = 32;
        } else if (arg instanceof Long) {
            v = (Long) arg;
            bits = 64;
        } else if (arg instanceof Short) {
            v = (Short) arg;
            bits = 16;
        } else if (arg instanceof Byte) {
            v = (Byte) arg;
            bits = 8;
        } else {
            if (arg instanceof Number) {
                throw new UnsupportedOperationException("formatting " + arg.getClass().getName() + " is not supported");
            }
            throw badArg(conv, arg);
        }
        String digits;
        boolean negative = false;
        if (conv == 'd') {
            negative = v < 0;
            digits = negative ? (v == Long.MIN_VALUE ? "9223372036854775808" : Long.toString(-v)) : Long.toString(v);
            if (flags.indexOf(',') >= 0) digits = group(digits, separators(l)[1]);
        } else {
            if (flags.indexOf('+') >= 0 || flags.indexOf(' ') >= 0 || flags.indexOf(',') >= 0 || flags.indexOf('(') >= 0) {
                throw new FormatFlagsConversionMismatchException("Conversion = " + conv + ", Flags = " + flags);
            }
            long u = bits == 64 ? v : v & ((1L << bits) - 1);
            digits = conv == 'x' ? Long.toHexString(u) : Long.toOctalString(u);
            if (flags.indexOf('#') >= 0) digits = (conv == 'x' ? "0x" : "0") + digits;
            if (upper) digits = digits.toUpperCase();
        }
        finishNumber(l, digits, negative, flags, width);
    }

    /** Applies sign, parentheses and zero padding, then width. */
    private void finishNumber(Locale l, String digits, boolean negative, String flags, int width) {
        String prefix = "", suffix = "";
        if (negative) {
            if (flags.indexOf('(') >= 0) {
                prefix = "(";
                suffix = ")";
            } else {
                prefix = "-";
            }
        } else if (flags.indexOf('+') >= 0) {
            prefix = "+";
        } else if (flags.indexOf(' ') >= 0) {
            prefix = " ";
        }
        if (flags.indexOf('0') >= 0) {
            if (width < 0) throw new MissingFormatWidthException("zero padding requires a width");
            StringBuilder sb = new StringBuilder(prefix);
            int hexPrefix = digits.startsWith("0x") || digits.startsWith("0X") ? 2 : 0;
            sb.append(digits, 0, hexPrefix);
            for (int k = prefix.length() + digits.length() + suffix.length(); k < width; k++) sb.append('0');
            sb.append(digits, hexPrefix, digits.length()).append(suffix);
            out.append(sb);
            return;
        }
        pad(l, prefix + digits + suffix, flags, width, true);
    }

    /** A null locale means no localization (Formatter's documented behaviour). */
    private static char[] separators(Locale l) {
        return l == null ? new char[] {'.', ','} : l.numberSeparators();
    }

    private static String group(String digits, char sep) {
        int intEnd = digits.indexOf('.');
        if (intEnd < 0) intEnd = digits.length();
        StringBuilder sb = new StringBuilder();
        for (int k = 0; k < intEnd; k++) {
            if (k > 0 && (intEnd - k) % 3 == 0) sb.append(sep);
            sb.append(digits.charAt(k));
        }
        sb.append(digits, intEnd, digits.length());
        return sb.toString();
    }

    private void floating(Locale l, char conv, boolean upper, String flags, int width, int precision, Object arg) {
        if (arg == null) {
            pad(l, "null", flags, width, false);
            return;
        }
        double v;
        if (arg instanceof Double) {
            v = (Double) arg;
        } else if (arg instanceof Float) {
            v = ((Float) arg).doubleValue();
        } else {
            if (arg instanceof Number && !(arg instanceof Integer || arg instanceof Long || arg instanceof Short
                    || arg instanceof Byte)) {
                throw new UnsupportedOperationException("formatting " + arg.getClass().getName() + " is not supported");
            }
            throw badArg(conv, arg);
        }
        if (v != v || Double.isInfinite(v)) {
            String s = v != v ? "NaN" : (v > 0 ? (flags.indexOf('+') >= 0 ? "+Infinity" : flags.indexOf(' ') >= 0 ? " Infinity" : "Infinity")
                    : (flags.indexOf('(') >= 0 ? "(Infinity)" : "-Infinity"));
            pad(l, upper ? s.toUpperCase() : s, flags.replace("0", ""), width, false);
            return;
        }
        boolean negative = Double.doubleToRawLongBits(v) < 0;
        double a = Math.abs(v);
        if (conv == 'a') {
            if (precision >= 0) throw new UnsupportedOperationException("%a with a precision is not supported");
            String s = Double.toHexString(a);
            if (upper) s = s.toUpperCase();
            finishNumber(l, s, negative, flags, width);
            return;
        }
        if (precision < 0) precision = 6;
        // Shortest decimal digits of `a` and the decimal exponent of the first one.
        // Float arguments are widened first, as java.util.Formatter does.
        Decimal d = Decimal.of(Double.toString(a));
        char[] seps = separators(l);
        String digits;
        if (conv == 'f') {
            digits = d.fixed(precision, flags.indexOf('#') >= 0, seps[0]);
            if (flags.indexOf(',') >= 0) digits = group(digits.replace(seps[0], '.'), seps[1]).replace('.', seps[0]);
        } else if (conv == 'e') {
            digits = d.scientific(precision, flags.indexOf('#') >= 0, seps[0]);
        } else {
            // %g: precision is significant digits; 0 means 1.
            int p = precision == 0 ? 1 : precision;
            Decimal r = d.roundSignificant(p);
            if (a != 0 && (r.exponent < -4 || r.exponent >= p)) {
                digits = d.scientific(p - 1, flags.indexOf('#') >= 0, seps[0]);
            } else {
                digits = d.fixed(Math.max(0, p - 1 - (a == 0 ? 0 : r.exponent)), flags.indexOf('#') >= 0, seps[0]);
                if (flags.indexOf(',') >= 0) digits = group(digits.replace(seps[0], '.'), seps[1]).replace('.', seps[0]);
            }
        }
        if (upper) digits = digits.toUpperCase();
        finishNumber(l, digits, negative, flags, width);
    }

    /** A non-negative decimal: digits d1 d2 ... dn with value 0.d1d2...dn * 10^(exponent+1). */
    private static final class Decimal {
        final String digits;  // no leading zeros unless the value is zero ("0")
        final int exponent;   // decimal exponent of the first digit

        Decimal(String digits, int exponent) {
            this.digits = digits;
            this.exponent = exponent;
        }

        /** Parses Double.toString / Float.toString output of a non-negative finite value. */
        static Decimal of(String s) {
            int e = 0;
            int ei = s.indexOf('E');
            if (ei >= 0) {
                e = Integer.parseInt(s.substring(ei + 1));
                s = s.substring(0, ei);
            }
            int dot = s.indexOf('.');
            String intPart = dot < 0 ? s : s.substring(0, dot);
            String frac = dot < 0 ? "" : s.substring(dot + 1);
            String all = intPart + frac;
            int exp = intPart.length() - 1 + e;
            int lead = 0;
            while (lead < all.length() - 1 && all.charAt(lead) == '0') lead++;
            exp -= lead;
            all = all.substring(lead);
            int end = all.length();
            while (end > 1 && all.charAt(end - 1) == '0') end--;
            all = all.substring(0, end);
            if (all.equals("0")) exp = 0;
            return new Decimal(all, exp);
        }

        boolean isZero() {
            return digits.equals("0");
        }

        /** Keeps `count` digits (count may be <= 0), rounding HALF_UP. */
        Decimal roundDigits(int count) {
            if (isZero() || count >= digits.length()) return this;
            if (count < 0) return new Decimal("0", 0);
            boolean up = digits.charAt(count) >= '5';
            char[] kept = digits.substring(0, count).toCharArray();
            int e = exponent;
            if (up) {
                int k = kept.length - 1;
                while (k >= 0 && kept[k] == '9') kept[k--] = '0';
                if (k >= 0) {
                    kept[k]++;
                } else {
                    char[] grown = new char[kept.length + 1];
                    grown[0] = '1';
                    System.arraycopy(kept, 0, grown, 1, kept.length);
                    kept = grown;
                    e++;
                }
            }
            String s = new String(kept);
            if (s.isEmpty()) return new Decimal("0", 0);
            int end = s.length();
            while (end > 1 && s.charAt(end - 1) == '0') end--;
            return new Decimal(s.substring(0, end), e);
        }

        Decimal roundSignificant(int p) {
            return roundDigits(p);
        }

        String fixed(int precision, boolean alwaysPoint, char point) {
            // Digits kept: those before the point plus `precision` after it.
            Decimal r = roundDigits(exponent + 1 + precision);
            StringBuilder sb = new StringBuilder();
            if (r.isZero() || r.exponent < 0) {
                sb.append('0');
            } else {
                for (int k = 0; k <= r.exponent; k++) sb.append(k < r.digits.length() ? r.digits.charAt(k) : '0');
            }
            if (precision > 0 || alwaysPoint) sb.append(point);
            for (int k = 1; k <= precision; k++) {
                int idx = r.exponent + k;  // digit index for 10^-k
                sb.append(!r.isZero() && idx >= 0 && idx < r.digits.length() ? r.digits.charAt(idx) : '0');
            }
            return sb.toString();
        }

        String scientific(int precision, boolean alwaysPoint, char point) {
            Decimal r = roundDigits(precision + 1);
            StringBuilder sb = new StringBuilder();
            sb.append(r.digits.charAt(0));
            if (precision > 0 || alwaysPoint) sb.append(point);
            for (int k = 1; k <= precision; k++) sb.append(k < r.digits.length() ? r.digits.charAt(k) : '0');
            int e = r.isZero() ? 0 : r.exponent;
            sb.append('e').append(e < 0 ? '-' : '+');
            int ae = Math.abs(e);
            if (ae < 10) sb.append('0');
            sb.append(ae);
            return sb.toString();
        }
    }
}
