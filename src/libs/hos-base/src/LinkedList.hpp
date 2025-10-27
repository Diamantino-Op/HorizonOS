#ifndef LIB_HOS_BASE_LINKEDLIST_HPP
#define LIB_HOS_BASE_LINKEDLIST_HPP

#import "Types.hpp"

template<class T>
struct LinkedListEntry {
    LinkedListEntry *next {};
    T *value {};
    LinkedListEntry *prev {};
};

template<class T>
class LinkedList {
public:
    ~LinkedList() {
        this->clear(true);
    }

    LinkedListEntry<T> *getFirst() {
        return this->listStart;
    }

    LinkedListEntry<T> *getLast() {
        return this->listEnd;
    }

    LinkedListEntry<T> *addStart(T *val) {
        auto* newEntry = new LinkedListEntry<T>();

        newEntry->value = val;

        this->addStart(newEntry);

        return newEntry;
    }

    void addStart(LinkedListEntry<T> *val) {
        if (this->listStart != nullptr) {
            this->listStart->prev = val;

            this->listStart = val;
        } else {
            this->listStart = val;
        }

        if (this->listEnd == nullptr) {
            this->listEnd = val;
        }

        this->size += 1;
    }

    LinkedListEntry<T> *addEnd(T *val) {
        auto* newEntry = new LinkedListEntry<T>();

        newEntry->value = val;

        this->addEnd(newEntry);

        return newEntry;
    }

    void addEnd(LinkedListEntry<T> *val) {
        if (this->listEnd != nullptr) {
            this->listEnd->next = val;

            this->listEnd = val;
        } else {
            this->listEnd = val;
        }

        if (this->listStart == nullptr) {
            this->listStart = val;
        }

        this->size += 1;
    }

    T *remove(LinkedListEntry<T> *val, const bool deleteValue) {
        LinkedListEntry<T>* current = this->listStart;

        while (current != nullptr) {
            if (current == val) {
                if (current->prev != nullptr) {
                    current->prev->next = current->next;
                }

                if (current->next != nullptr) {
                    current->next->prev = current->prev;
                }

                if (this->listStart == current) {
                    this->listStart = current->next;
                }

                if (this->listEnd == current) {
                    this->listEnd = current->prev;
                }

                if (deleteValue) {
                    delete current->value;

                    return nullptr;
                }

                T* tmpVal = current->value;

                delete current;

                return tmpVal;
            }
        }

        return nullptr;
    }

    bool remove(T *val, const bool deleteValue = true) {
        LinkedListEntry<T>* current = this->listStart;

        while (current != nullptr) {
            if (current->value == val) {
                if (current->prev != nullptr) {
                    current->prev->next = current->next;
                }

                if (current->next != nullptr) {
                    current->next->prev = current->prev;
                }

                if (this->listStart == current) {
                    this->listStart = current->next;
                }

                if (this->listEnd == current) {
                    this->listEnd = current->prev;
                }

                if (deleteValue) {
                    delete current->value;
                }

                delete current;

                return true;
            }

            current = current->next;
        }

        return false;
    }

    bool removeEntry(LinkedListEntry<T> *val) {
        if (val == nullptr) {
            return false;
        }

        if (this->listStart == val) {
            this->listStart = this->listStart->next;
        }

        if (this->listEnd == val) {
            this->listEnd = this->listEnd->prev;
        }

        if (val->next != nullptr) {
            val->next->prev = val->prev;
        }

        if (val->prev != nullptr) {
            val->prev->next = val->next;
        }

        val->next = nullptr;
        val->prev = nullptr;

        return true;
    }

    T *removeFirst(const bool deleteValue = true) {
        LinkedListEntry<T>* current = this->listStart;

        this->listStart->next->prev = nullptr;
        this->listStart = this->listStart->next;

        this->size -= 1;

        if (this->listEnd == current) {
            this->listEnd = nullptr;
        }

        if (deleteValue) {
            delete current->value;
            delete current;

            return nullptr;
        }

        T* tmpVal = current->value;

        delete current;

        return tmpVal;
    }

    LinkedListEntry<T> *removeFirstEntry() {
        LinkedListEntry<T> *tmpEntry = this->listStart;

        this->removeEntry(tmpEntry);

        return tmpEntry;
    }

    T *removeLast(const bool deleteValue = true) {
        LinkedListEntry<T>* current = this->listEnd;

        this->listEnd->prev->next = nullptr;
        this->listEnd = this->listEnd->prev;

        this->size -= 1;

        if (this->listStart == current) {
            this->listStart = nullptr;
        }

        if (deleteValue) {
            delete current->value;
            delete current;

            return nullptr;
        }

        T* tmpVal = current->value;

        delete current;

        return tmpVal;
    }

    LinkedListEntry<T> *removeLastEntry() {
        LinkedListEntry<T> *tmpEntry = this->listEnd;

        this->removeEntry(tmpEntry);

        return tmpEntry;
    }

    void clear(const bool deleteValues = true) {
        LinkedListEntry<T>* current = this->listStart;

        while (current != nullptr) {
            LinkedListEntry<T>* tmpCurrent = current->next;

            if (deleteValues) {
                delete current->value;
            }

            delete current;

            current = tmpCurrent;
        }
    }

    u32 getSize() const {
        return this->size;
    }

    // Iterator

    class Iterator {
    public:
        explicit Iterator(LinkedListEntry<T>* ptr) : current(ptr) {}

        T& operator*() const {
            return *(current->value);
        }

        T* operator->() const {
            return current->value;
        }

        Iterator& operator++ () {
            if (current) {
                current = current->next;
            }

            return *this;
        }

        Iterator operator++ (const int amount) {
            Iterator temp = *this;

            for (u32 i = 0; i < amount; i++) {
                ++(*this);
            }

            return temp;
        }

        Iterator& operator-- () {
            if (current) {
                current = current->prev;
            }

            return *this;
        }

        Iterator operator-- (const int amount) {
            Iterator temp = *this;

            for (u32 i = 0; i < amount; i++) {
                --(*this);
            }

            return temp;
        }

        bool operator== (const Iterator& other) const {
            return current == other.current;
        }

        bool operator!= (const Iterator& other) const {
            return current != other.current;
        }

    private:

        LinkedListEntry<T>* current;
    };

    Iterator begin() {
        return Iterator(this->listStart);
    }

    Iterator end() {
        return Iterator(nullptr);
    }

    bool contains(T *val) {
        for (const T& currVal : *this) {
            if (&currVal == val) {
                return true;
            }
        }

        return false;
    }

private:

    LinkedListEntry<T>* listStart {};
    LinkedListEntry<T>* listEnd {};

    u32 size {};
};

#endif