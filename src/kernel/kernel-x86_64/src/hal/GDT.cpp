#include "GDT.hpp"

namespace hal::x86_64 {
    void GdtManager::initGdt() {

    }

    void GdtManager::loadGdt() {

    }

    void GdtManager::addGdtEntry(u8 flags, u8 granularity) {

    }

    void GdtManager::addTssEntry() {

    }

    Gdt GdtManager::getGdt() {
        return this->gdtInstance;
    }
}