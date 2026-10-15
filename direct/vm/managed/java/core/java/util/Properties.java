package java.util;

/** String key/value table; System.getProperties() is a snapshot of the VM's properties. */
public class Properties extends HashMap<Object, Object> {
    protected Properties defaults;

    public Properties() {}

    public Properties(Properties defaults) {
        this.defaults = defaults;
    }

    public synchronized Object setProperty(String key, String value) {
        return put(key, value);
    }

    public String getProperty(String key) {
        Object v = get(key);
        if (v instanceof String) return (String) v;
        return defaults != null ? defaults.getProperty(key) : null;
    }

    public String getProperty(String key, String defaultValue) {
        String v = getProperty(key);
        return v != null ? v : defaultValue;
    }

    public Set<String> stringPropertyNames() {
        HashSet<String> names = new HashSet<String>();
        if (defaults != null) names.addAll(defaults.stringPropertyNames());
        for (Object k : keySet()) {
            if (k instanceof String) names.add((String) k);
        }
        return names;
    }
}
