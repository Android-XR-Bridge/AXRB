package java.lang;

/** Name only: this runtime has no JAR manifests to read package versions from. */
public class Package {
    private final String name;

    Package(String name) {
        this.name = name;
    }

    public String getName() {
        return name;
    }

    public String getImplementationTitle() {
        return null;
    }

    public String getImplementationVersion() {
        return null;
    }

    public String getImplementationVendor() {
        return null;
    }

    public String getSpecificationTitle() {
        return null;
    }

    public String getSpecificationVersion() {
        return null;
    }

    public String getSpecificationVendor() {
        return null;
    }

    public static Package getPackage(String name) {
        return name == null || name.isEmpty() ? null : new Package(name);
    }

    public int hashCode() {
        return name.hashCode();
    }

    public boolean equals(Object o) {
        return o instanceof Package && name.equals(((Package) o).name);
    }

    public String toString() {
        return "package " + name;
    }
}
