#ifndef LIB_HOS_BASE_LINKEDLIST_HPP
#define LIB_HOS_BASE_LINKEDLIST_HPP

#include "Types.hpp"
#include "SpinLock.hpp"

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

    LinkedListEntry<T> *addStart(T *val, bool useLock = true) {
        auto* newEntry = new LinkedListEntry<T>();

        newEntry->value = val;

        this->addStart(newEntry, useLock);

        return newEntry;
    }

    void addStart(LinkedListEntry<T> *val, bool useLock = true) {
    	bool prevIF = true;

    	if (useLock) {
    		prevIF = this->listLock.lock();
    	}

        if (this->listStart != nullptr) {
            this->listStart->prev = val;

            val->next = this->listStart;

            this->listStart = val;
        } else {
            this->listStart = val;
        }

        if (this->listEnd == nullptr) {
            this->listEnd = val;
        }

        this->size += 1;

        if (useLock) {
        	this->listLock.unlock(prevIF);
        }
    }

    LinkedListEntry<T> *addEnd(T *val, bool useLock = true) {
        auto* newEntry = new LinkedListEntry<T>();

        newEntry->value = val;

        this->addEnd(newEntry, useLock);

        return newEntry;
    }

    void addEnd(LinkedListEntry<T> *val, bool useLock = true) {
    	bool prevIF = true;

    	if (useLock) {
    		prevIF = this->listLock.lock();
    	}

        if (this->listEnd != nullptr) {
            this->listEnd->next = val;

            val->prev = this->listEnd;

            this->listEnd = val;
        } else {
            this->listEnd = val;
        }

        if (this->listStart == nullptr) {
            this->listStart = val;
        }

        this->size += 1;

    	if (useLock) {
    		this->listLock.unlock(prevIF);
    	}
    }

    T *remove(LinkedListEntry<T> *val, const bool deleteValue, bool useLock = true) {
    	bool prevIF = true;

    	if (useLock) {
    		prevIF = this->listLock.lock();
    	}

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

                this->size -= 1;

                if (deleteValue) {
                    delete current->value;
                	delete current;

                	if (useLock) {
                		this->listLock.unlock(prevIF);
                	}

                    return nullptr;
                }

                T* tmpVal = current->value;

                delete current;

            	if (useLock) {
            		this->listLock.unlock(prevIF);
            	}

                return tmpVal;
            }

              current = current->next;
        }

    	if (useLock) {
    		this->listLock.unlock(prevIF);
    	}

        return nullptr;
    }

    bool remove(T *val, const bool deleteValue = true, bool useLock = true) {
    	bool prevIF = true;

    	if (useLock) {
    		prevIF = this->listLock.lock();
    	}

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

                this->size -= 1;

            	if (useLock) {
            		this->listLock.unlock(prevIF);
            	}

                return true;
            }

            current = current->next;
        }

    	if (useLock) {
    		this->listLock.unlock(prevIF);
    	}

        return false;
    }

    bool removeEntry(LinkedListEntry<T> *val, bool useLock = true) {
        if (val == nullptr) {
            return false;
        }

    	bool prevIF = true;

    	if (useLock) {
    		prevIF = this->listLock.lock();
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

        this->size -= 1;

    	if (useLock) {
    		this->listLock.unlock(prevIF);
    	}

        return true;
    }

    T *removeFirst(const bool deleteValue = true, bool useLock = true) {
    	bool prevIF = true;

    	if (useLock) {
    		prevIF = this->listLock.lock();
    	}

        LinkedListEntry<T>* current = this->listStart;

    	if (current == nullptr) {
    		if (useLock) {
    			this->listLock.unlock(prevIF);
    		}

    		return nullptr;
    	}

    	if (current->next != nullptr) {
    		current->next->prev = nullptr;
    	}

    	this->listStart = current->next;

    	if (this->listStart != nullptr) {
    		this->listStart->prev = nullptr;
    	} else {
    		this->listEnd = nullptr;
    	}

        this->size -= 1;

        if (deleteValue) {
            delete current->value;
            delete current;

        	if (useLock) {
        		this->listLock.unlock(prevIF);
        	}

            return nullptr;
        }

        T* tmpVal = current->value;

        delete current;

    	if (useLock) {
    		this->listLock.unlock(prevIF);
    	}

        return tmpVal;
    }

    LinkedListEntry<T> *removeFirstEntry(bool useLock = true) {
    	bool prevIF = true;

    	if (useLock) {
    		prevIF = this->listLock.lock();
    	}

        LinkedListEntry<T> *tmpEntry = this->listStart;

        if (tmpEntry == nullptr) {
        	if (useLock) {
        		this->listLock.unlock(prevIF);
        	}

            return nullptr;
        }

        this->listStart = tmpEntry->next;

        if (this->listStart != nullptr) {
            this->listStart->prev = nullptr;
        } else {
            this->listEnd = nullptr;
        }

        tmpEntry->next = nullptr;
        tmpEntry->prev = nullptr;

        this->size -= 1;

    	if (useLock) {
    		this->listLock.unlock(prevIF);
    	}

        return tmpEntry;
    }

    T *removeLast(const bool deleteValue = true, bool useLock = true) {
    	bool prevIF = true;

    	if (useLock) {
    		prevIF = this->listLock.lock();
    	}

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

        	if (useLock) {
        		this->listLock.unlock(prevIF);
        	}

            return nullptr;
        }

        T* tmpVal = current->value;

        delete current;

    	if (useLock) {
    		this->listLock.unlock(prevIF);
    	}

        return tmpVal;
    }

    LinkedListEntry<T> *removeLastEntry(bool useLock = true) {
    	bool prevIF = true;

    	if (useLock) {
    		prevIF = this->listLock.lock();
    	}

        LinkedListEntry<T> *tmpEntry = this->listEnd;

        if (tmpEntry == nullptr) {
        	if (useLock) {
        		this->listLock.unlock(prevIF);
        	}

            return nullptr;
        }

        this->listEnd = tmpEntry->prev;

        if (this->listEnd != nullptr) {
            this->listEnd->next = nullptr;
        } else {
            this->listStart = nullptr;
        }

        tmpEntry->next = nullptr;
        tmpEntry->prev = nullptr;

        this->size -= 1;

    	if (useLock) {
    		this->listLock.unlock(prevIF);
    	}

        return tmpEntry;
    }

    void clear(const bool deleteValues = true, bool useLock = true) {
    	bool prevIF = true;

    	if (useLock) {
    		prevIF = this->listLock.lock();
    	}

        LinkedListEntry<T>* current = this->listStart;

        while (current != nullptr) {
            LinkedListEntry<T>* tmpCurrent = current->next;

            if (deleteValues) {
                delete current->value;
            }

            delete current;

            current = tmpCurrent;
        }

        this->size = 0;

    	if (useLock) {
    		this->listLock.unlock(prevIF);
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

        Iterator operator++ (int) {
            Iterator temp = *this;
            ++(*this);
            return temp;
        }

        Iterator& operator-- () {
            if (current) {
                current = current->prev;
            }

            return *this;
        }

        Iterator operator-- (int) {
            Iterator temp = *this;
            --(*this);
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

    Iterator begin() const {
        return Iterator(this->listStart);
    }

    Iterator end() const {
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

    TicketSpinLock listLock {}; // TODO: Maybe remove this spinlock
};

#endif