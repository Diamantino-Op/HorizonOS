#include "IDT.hpp"

#include "Main.hpp"

namespace kernel::x86_64::hal {
	IdtManager::IdtManager() {
		this->idtInstance = Idt();
	}

	void IdtManager::addEntry(u8 id, usize handler, u16 selector, u8 ist, u8 flags) {
		this->idtInstance.entries[id] = IDTEntry(handler, selector, ist, flags);
    }

	void IdtManager::loadIdt() {
		Terminal* terminal = CommonMain::getTerminal();

		this->idtDescriptor = IDTDesc(this->idtInstance);

		terminal->debug("Loading IDT at address: 0x%.16lx", "IDT", &this->idtDescriptor);

		loadIdtAsm(&this->idtDescriptor);
	}

	Idt IdtManager::getIdt() {
		return this->idtInstance;
	}
}