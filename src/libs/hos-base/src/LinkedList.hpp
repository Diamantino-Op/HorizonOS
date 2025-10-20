#ifndef LIB_HOS_BASE_LINKEDLIST_HPP
#define LIB_HOS_BASE_LINKEDLIST_HPP

template<class T>
struct LinkedListEntry {
    LinkedListEntry* next;
    T* value;
    LinkedListEntry* prev;
};

template<class T>
class LinkedList {
public:
    LinkedList();
    ~LinkedList();

    LinkedListEntry<T>* getFirst() {

    }

    LinkedListEntry<T>* getLast() {

    }

    void addStart(T* val) {

    }

    void addEnd(T* val) {

    }

    void remove(LinkedListEntry<T>* val) {

    }

    void removeFirst() {

    }

    void removeLast() {

    }

    void clear() {

    }

    int getSize() const {
        return this->size;
    }

private:

    LinkedListEntry<T>* listStar;
    LinkedListEntry<T>* listEnd;

    int size;
};

#endif