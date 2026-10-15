package android.util;

/**
 * The attributes of one XML element, typically the current tag of a compiled
 * (binary) XML parser. Values keep their compiled types: a reference is a
 * resource id, a color an int, and so on.
 */
public interface AttributeSet {
    int getAttributeCount();

    default String getAttributeNamespace(int index) {
        return null;
    }

    String getAttributeName(int index);

    String getAttributeValue(int index);

    String getAttributeValue(String namespace, String name);

    String getPositionDescription();

    int getAttributeNameResource(int index);

    int getAttributeListValue(String namespace, String attribute, String[] options, int defaultValue);

    boolean getAttributeBooleanValue(String namespace, String attribute, boolean defaultValue);

    int getAttributeResourceValue(String namespace, String attribute, int defaultValue);

    int getAttributeIntValue(String namespace, String attribute, int defaultValue);

    int getAttributeUnsignedIntValue(String namespace, String attribute, int defaultValue);

    float getAttributeFloatValue(String namespace, String attribute, float defaultValue);

    int getAttributeListValue(int index, String[] options, int defaultValue);

    boolean getAttributeBooleanValue(int index, boolean defaultValue);

    int getAttributeResourceValue(int index, int defaultValue);

    int getAttributeIntValue(int index, int defaultValue);

    int getAttributeUnsignedIntValue(int index, int defaultValue);

    float getAttributeFloatValue(int index, float defaultValue);

    String getIdAttribute();

    String getClassAttribute();

    int getIdAttributeResourceValue(int defaultValue);

    int getStyleAttribute();
}
