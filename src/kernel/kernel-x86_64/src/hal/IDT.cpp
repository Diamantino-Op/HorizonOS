#include "IDT.hpp"

#include "Main.hpp"

namespace kernel::x86_64::hal {
	IdtManager::IdtManager() {
		this->idtInstance = Idt();
	}

	void IdtManager::addEntry(const u8 id, const usize handler, const u16 selector, const u8 ist, const u8 flags) {
		this->idtInstance.entries[id] = IDTEntry(handler, selector, ist, flags | IDT_ENTRY_PRESENT);
    }

	void IdtManager::loadIdt() {
		Terminal* terminal = CommonMain::getTerminal();

		this->idtDescriptor = IDTDesc(this->idtInstance);

		terminal->debug("Loading IDT at address: 0x%.16lx", "IDT", &this->idtDescriptor);

		loadIdtAsm(&this->idtDescriptor);
	}

	Idt IdtManager::getIdt() const {
		return this->idtInstance;
	}
}