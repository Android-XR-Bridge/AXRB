package java.util;

public class Stack<E> extends Vector<E> {
    public Stack() {}

    public E push(E item) {
        addElement(item);
        return item;
    }

    public synchronized E pop() {
        E obj = peek();
        removeElementAt(size() - 1);
        return obj;
    }

    public synchronized E peek() {
        if (size() == 0) throw new EmptyStackException();
        return elementAt(size() - 1);
    }

    public boolean empty() {
        return size() == 0;
    }

    public synchronized int search(Object o) {
        int i = lastIndexOf(o);
        return i >= 0 ? size() - i : -1;
    }
}
