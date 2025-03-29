#include "IDT.hpp"

namespace x86_64::hal {
	IDTManager::IDTManager() {
		this->idtInstance = Idt();
	}

	void IDTManager::addEntry(u8 id, usize handler, u16 selector, u8 ist, u8 flags) {
		this->idtInstance.entries[id] = IDTEntry(handler, selector, ist, flags);
    }

	void IDTManager::loadIdt() {
		this->idtDescriptor = IDTDesc(this->idtInstance);

		loadIdtAsm(&this->idtDescriptor);
	}

	Idt IDTManager::getIdt() {
		return this->idtInstance;
	}

	void handleInterruptAsm(usize stackFrame) {

	}
}