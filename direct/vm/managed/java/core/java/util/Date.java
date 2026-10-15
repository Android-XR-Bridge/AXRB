package java.util;

public class Date implements java.io.Serializable, Cloneable, Comparable<Date> {
    private long time;

    public Date() {
        this(System.currentTimeMillis());
    }

    public Date(long date) {
        time = date;
    }

    // The deprecated calendar fields, in UTC (the runtime's only time zone).
    private long[] civil() {
        long days = Math.floorDiv(getTime(), 86400000L);
        long ms = Math.floorMod(getTime(), 86400000L);
        long z = days + 719468, era = Math.floorDiv(z, 146097), doe = z - era * 146097;
        long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        long doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
        long d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
        long y = yoe + era * 400 + (m <= 2 ? 1 : 0);
        return new long[] {y, m, d, ms / 3600000, ms / 60000 % 60, ms / 1000 % 60, Math.floorMod(days + 4, 7)};
    }

    @Deprecated
    public int getYear() {
        return (int) civil()[0] - 1900;
    }

    @Deprecated
    public int getMonth() {
        return (int) civil()[1] - 1;
    }

    @Deprecated
    public int getDate() {
        return (int) civil()[2];
    }

    @Deprecated
    public int getHours() {
        return (int) civil()[3];
    }

    @Deprecated
    public int getMinutes() {
        return (int) civil()[4];
    }

    @Deprecated
    public int getSeconds() {
        return (int) civil()[5];
    }

    @Deprecated
    public int getDay() {
        return (int) civil()[6];
    }

    public long getTime() {
        return time;
    }

    public void setTime(long time) {
        this.time = time;
    }

    public boolean before(Date when) {
        return time < when.time;
    }

    public boolean after(Date when) {
        return time > when.time;
    }

    public int compareTo(Date other) {
        return Long.compare(time, other.time);
    }

    public boolean equals(Object obj) {
        return obj instanceof Date && ((Date) obj).time == time;
    }

    public int hashCode() {
        return (int) time ^ (int) (time >> 32);
    }

    public Object clone() {
        return new Date(time);
    }

    public String toString() {
        return "Date(" + time + ")";
    }
}
