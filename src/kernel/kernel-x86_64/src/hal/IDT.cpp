#include "IDT.hpp"

namespace x86_64::hal {
	IDTManager::IDTManager() {
		this->idtInstance = Idt();
	}

	void IDTManager::loadIdt() {
		this->idtDescriptor = IDTDesc(this->idtInstance);

		loadIdtAsm(&this->idtDescriptor);
	}

	Idt IDTManager::getIdt() {
		return this->idtInstance;
	}
}