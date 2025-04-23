#include "Apic.hpp"

namespace kernel::x86_64::hal {
	void Apic::init() {

	}

	void Apic::setId(const u32 apicId) {
		this->apicId = apicId;
	}

	u32 Apic::getId() const {
		return this->apicId;
	}

}