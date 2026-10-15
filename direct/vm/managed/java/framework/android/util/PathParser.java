package android.util;

import android.graphics.Path;

/** Parses SVG path data (VectorDrawable pathData) into a Path. */
public class PathParser {
    private PathParser() {}

    public static Path createPathFromPathData(String pathData) {
        Path path = new Path();
        if (pathData != null) new Parser(pathData).parseInto(path);
        return path;
    }

    public static boolean canMorph(String a, String b) {
        return false;
    }

    private static final class Parser {
        private final String s;
        private int pos;
        private float curX, curY, ctrlX, ctrlY, startX, startY;
        private char prevCmd = ' ';

        Parser(String s) {
            this.s = s;
        }

        private void skipSeparators() {
            while (pos < s.length()) {
                char c = s.charAt(pos);
                if (c == ' ' || c == ',' || c == '\t' || c == '\n' || c == '\r') pos++; else break;
            }
        }

        private boolean atNumber() {
            skipSeparators();
            if (pos >= s.length()) return false;
            char c = s.charAt(pos);
            return (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.';
        }

        private float number() {
            skipSeparators();
            int start = pos;
            if (pos < s.length() && (s.charAt(pos) == '-' || s.charAt(pos) == '+')) pos++;
            boolean dot = false, exp = false;
            while (pos < s.length()) {
                char c = s.charAt(pos);
                if (c >= '0' && c <= '9') {
                    pos++;
                } else if (c == '.' && !dot && !exp) {
                    dot = true;
                    pos++;
                } else if ((c == 'e' || c == 'E') && !exp) {
                    exp = true;
                    pos++;
                    if (pos < s.length() && (s.charAt(pos) == '-' || s.charAt(pos) == '+')) pos++;
                } else {
                    break;
                }
            }
            if (start == pos) throw new RuntimeException("error in parsing \"" + s + "\" at " + pos);
            return Float.parseFloat(s.substring(start, pos));
        }

        private boolean flag() {
            skipSeparators();
            if (pos < s.length() && (s.charAt(pos) == '0' || s.charAt(pos) == '1')) return s.charAt(pos++) == '1';
            return number() != 0;
        }

        void parseInto(Path p) {
            char cmd = ' ';
            while (true) {
                skipSeparators();
                if (pos >= s.length()) break;
                char c = s.charAt(pos);
                if (Character.isLetter(c) && c != 'e' && c != 'E') {
                    cmd = c;
                    pos++;
                } else if (cmd == ' ') {
                    throw new RuntimeException("error in parsing \"" + s + "\": no command");
                } else if (cmd == 'M') {
                    cmd = 'L';
                } else if (cmd == 'm') {
                    cmd = 'l';
                }
                exec(p, cmd);
                prevCmd = cmd;
                if (cmd == 'z' || cmd == 'Z') continue;
                // Repeated coordinates reuse the command (handled by the loop).
            }
        }

        private void exec(Path p, char cmd) {
            switch (cmd) {
                case 'M': case 'm': {
                    float x = number(), y = number();
                    if (cmd == 'm') {
                        x += curX;
                        y += curY;
                    }
                    p.moveTo(x, y);
                    curX = startX = ctrlX = x;
                    curY = startY = ctrlY = y;
                    break;
                }
                case 'L': case 'l': {
                    float x = number(), y = number();
                    if (cmd == 'l') {
                        x += curX;
                        y += curY;
                    }
                    p.lineTo(x, y);
                    curX = ctrlX = x;
                    curY = ctrlY = y;
                    break;
                }
                case 'H': case 'h': {
                    float x = number();
                    if (cmd == 'h') x += curX;
                    p.lineTo(x, curY);
                    curX = ctrlX = x;
                    ctrlY = curY;
                    break;
                }
                case 'V': case 'v': {
                    float y = number();
                    if (cmd == 'v') y += curY;
                    p.lineTo(curX, y);
                    curY = ctrlY = y;
                    ctrlX = curX;
                    break;
                }
                case 'C': case 'c': {
                    float x1 = number(), y1 = number(), x2 = number(), y2 = number(), x = number(), y = number();
                    if (cmd == 'c') {
                        x1 += curX; y1 += curY; x2 += curX; y2 += curY; x += curX; y += curY;
                    }
                    p.cubicTo(x1, y1, x2, y2, x, y);
                    ctrlX = x2;
                    ctrlY = y2;
                    curX = x;
                    curY = y;
                    break;
                }
                case 'S': case 's': {
                    float x2 = number(), y2 = number(), x = number(), y = number();
                    if (cmd == 's') {
                        x2 += curX; y2 += curY; x += curX; y += curY;
                    }
                    float x1 = curX, y1 = curY;
                    if ("CcSs".indexOf(prevCmd) >= 0) {
                        x1 = 2 * curX - ctrlX;
                        y1 = 2 * curY - ctrlY;
                    }
                    p.cubicTo(x1, y1, x2, y2, x, y);
                    ctrlX = x2;
                    ctrlY = y2;
                    curX = x;
                    curY = y;
                    break;
                }
                case 'Q': case 'q': {
                    float x1 = number(), y1 = number(), x = number(), y = number();
                    if (cmd == 'q') {
                        x1 += curX; y1 += curY; x += curX; y += curY;
                    }
                    p.quadTo(x1, y1, x, y);
                    ctrlX = x1;
                    ctrlY = y1;
                    curX = x;
                    curY = y;
                    break;
                }
                case 'T': case 't': {
                    float x = number(), y = number();
                    if (cmd == 't') {
                        x += curX;
                        y += curY;
                    }
                    float x1 = curX, y1 = curY;
                    if ("QqTt".indexOf(prevCmd) >= 0) {
                        x1 = 2 * curX - ctrlX;
                        y1 = 2 * curY - ctrlY;
                    }
                    p.quadTo(x1, y1, x, y);
                    ctrlX = x1;
                    ctrlY = y1;
                    curX = x;
                    curY = y;
                    break;
                }
                case 'A': case 'a': {
                    float rx = number(), ry = number(), rot = number();
                    boolean large = flag(), sweep = flag();
                    float x = number(), y = number();
                    if (cmd == 'a') {
                        x += curX;
                        y += curY;
                    }
                    arc(p, curX, curY, x, y, rx, ry, rot, large, sweep);
                    curX = ctrlX = x;
                    curY = ctrlY = y;
                    break;
                }
                case 'Z': case 'z':
                    p.close();
                    curX = ctrlX = startX;
                    curY = ctrlY = startY;
                    break;
                default:
                    throw new RuntimeException("error in parsing \"" + s + "\": unknown command " + cmd);
            }
        }

        /** SVG elliptical arc as cubic Béziers (endpoint to center parameterization). */
        private static void arc(Path p, float x0, float y0, float x1, float y1, float a, float b, float theta,
                boolean isMoreThanHalf, boolean isPositiveArc) {
            if (a == 0 || b == 0) {
                p.lineTo(x1, y1);
                return;
            }
            double thetaD = Math.toRadians(theta);
            double cosTheta = Math.cos(thetaD);
            double sinTheta = Math.sin(thetaD);
            double x0p = (x0 * cosTheta + y0 * sinTheta) / a;
            double y0p = (-x0 * sinTheta + y0 * cosTheta) / b;
            double x1p = (x1 * cosTheta + y1 * sinTheta) / a;
            double y1p = (-x1 * sinTheta + y1 * cosTheta) / b;
            double dx = x0p - x1p;
            double dy = y0p - y1p;
            double xm = (x0p + x1p) / 2;
            double ym = (y0p + y1p) / 2;
            double dsq = dx * dx + dy * dy;
            if (dsq == 0.0) return;
            double disc = 1.0 / dsq - 1.0 / 4.0;
            if (disc < 0.0) {
                float adjust = (float) (Math.sqrt(dsq) / 1.99999);
                arc(p, x0, y0, x1, y1, a * adjust, b * adjust, theta, isMoreThanHalf, isPositiveArc);
                return;
            }
            double sq = Math.sqrt(disc);
            double eDx = sq * dx;
            double eDy = sq * dy;
            double cx, cy;
            if (isMoreThanHalf == isPositiveArc) {
                cx = xm - eDy;
                cy = ym + eDx;
            } else {
                cx = xm + eDy;
                cy = ym - eDx;
            }
            double eta0 = Math.atan2((y0p - cy), (x0p - cx));
            double eta1 = Math.atan2((y1p - cy), (x1p - cx));
            double sweep = (eta1 - eta0);
            if (isPositiveArc != (sweep >= 0)) {
                if (sweep > 0) sweep -= 2 * Math.PI; else sweep += 2 * Math.PI;
            }
            cx *= a;
            cy *= b;
            double tcx = cx;
            cx = cx * cosTheta - cy * sinTheta;
            cy = tcx * sinTheta + cy * cosTheta;
            arcToBezier(p, cx, cy, a, b, x0, y0, thetaD, eta0, sweep);
        }

        private static void arcToBezier(Path p, double cx, double cy, double a, double b, double e1x, double e1y,
                double theta, double start, double sweep) {
            int numSegments = (int) Math.ceil(Math.abs(sweep * 4 / Math.PI));
            double eta1 = start;
            double cosTheta = Math.cos(theta);
            double sinTheta = Math.sin(theta);
            double cosEta1 = Math.cos(eta1);
            double sinEta1 = Math.sin(eta1);
            double ep1x = (-a * cosTheta * sinEta1) - (b * sinTheta * cosEta1);
            double ep1y = (-a * sinTheta * sinEta1) + (b * cosTheta * cosEta1);
            double anglePerSegment = sweep / numSegments;
            for (int i = 0; i < numSegments; i++) {
                double eta2 = eta1 + anglePerSegment;
                double sinEta2 = Math.sin(eta2);
                double cosEta2 = Math.cos(eta2);
                double e2x = cx + (a * cosTheta * cosEta2) - (b * sinTheta * sinEta2);
                double e2y = cy + (a * sinTheta * cosEta2) + (b * cosTheta * sinEta2);
                double ep2x = -a * cosTheta * sinEta2 - b * sinTheta * cosEta2;
                double ep2y = -a * sinTheta * sinEta2 + b * cosTheta * cosEta2;
                double tanDiff2 = Math.tan((eta2 - eta1) / 2);
                double alpha = Math.sin(eta2 - eta1) * (Math.sqrt(4 + (3 * tanDiff2 * tanDiff2)) - 1) / 3;
                double q1x = e1x + alpha * ep1x;
                double q1y = e1y + alpha * ep1y;
                double q2x = e2x - alpha * ep2x;
                double q2y = e2y - alpha * ep2y;
                p.cubicTo((float) q1x, (float) q1y, (float) q2x, (float) q2y, (float) e2x, (float) e2y);
                eta1 = eta2;
                e1x = e2x;
                e1y = e2y;
                ep1x = ep2x;
                ep1y = ep2y;
            }
        }
    }
}
