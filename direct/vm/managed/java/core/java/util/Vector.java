package java.util;

public class Vector<E> extends ArrayList<E> {
    public Vector() {
        super();
    }

    public Vector(int initialCapacity) {
        super(initialCapacity);
    }

    public Vector(int initialCapacity, int capacityIncrement) {
        super(initialCapacity);
    }

    public Vector(Collection<? extends E> c) {
        super(c);
    }

    public synchronized E get(int index) {
        return super.get(index);
    }

    public synchronized E set(int index, E element) {
        return super.set(index, element);
    }

    public synchronized boolean add(E e) {
        return super.add(e);
    }

    public synchronized void add(int index, E element) {
        super.add(index, element);
    }

    public synchronized E remove(int index) {
        return super.remove(index);
    }

    public synchronized boolean remove(Object o) {
        return super.remove(o);
    }

    public synchronized void clear() {
        super.clear();
    }

    public synchronized int size() {
        return super.size();
    }

    public synchronized E elementAt(int index) {
        return get(index);
    }

    public synchronized E firstElement() {
        if (isEmpty()) throw new NoSuchElementException();
        return get(0);
    }

    public synchronized E lastElement() {
        if (isEmpty()) throw new NoSuchElementException();
        return get(size() - 1);
    }

    public synchronized void addElement(E obj) {
        add(obj);
    }

    public synchronized void insertElementAt(E obj, int index) {
        add(index, obj);
    }

    public synchronized boolean removeElement(Object obj) {
        return remove(obj);
    }

    public synchronized void removeElementAt(int index) {
        remove(index);
    }

    public synchronized void removeAllElements() {
        clear();
    }

    public synchronized void setElementAt(E obj, int index) {
        set(index, obj);
    }

    public synchronized void copyInto(Object[] anArray) {
        Object[] a = toArray();
        System.arraycopy(a, 0, anArray, 0, a.length);
    }

    public Enumeration<E> elements() {
        final Iterator<E> it = new ArrayList<E>(this).iterator();
        return new Enumeration<E>() {
            public boolean hasMoreElements() {
                return it.hasNext();
            }

            public E nextElement() {
                return it.next();
            }
        };
    }
}
