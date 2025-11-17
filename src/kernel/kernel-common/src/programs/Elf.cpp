#include "Elf.hpp"

#include "CommonMain.hpp"

namespace kernel::common::programs {
	u64 *Elf::loadElf(const u64 *elfFile, AllocContext *ctx) {
		auto *elfHeader = reinterpret_cast<const ElfCommonHeader *>(elfFile);

		if (not isSupported(elfHeader)) {
			CommonMain::getTerminal()->error("ELF is not supported!", "Elf Loader");

			return nullptr;
		}

		switch (elfHeader->elfType) {
			case ElfType::ET_REL:
				return loadRel(elfFile, ctx);

			case ElfType::ET_EXEC:
				return loadExe(elfFile, ctx);

			case ElfType::ET_DYN:
				return loadExeDyn(elfFile, ctx);

			default:
				CommonMain::getTerminal()->error("ELF type is not supported!", "Elf Loader");

				return nullptr;
		}
	}

	u64 *Elf::loadRel(const u64 *elfFile, AllocContext *ctx) {
		return nullptr;
	}

	u64 *Elf::loadExeDyn(const u64 *elfFile, AllocContext *ctx) {
		return nullptr;
	}

	u64 *Elf::loadExe(const u64 *elfFile, AllocContext *ctx) {
		return nullptr;
	}

	bool Elf::isElf(const ElfCommonHeader *elfHeader) {
		if(not elfHeader) {
			return false;
		}

		if(elfHeader->elfIdentity[ElfIdent::ELF_ID_MAG0] != ElfMagic0) {
			CommonMain::getTerminal()->error("ELF Header Magic 0 is incorrect!", "Elf Loader");

			return false;
		}

		if(elfHeader->elfIdentity[ElfIdent::ELF_ID_MAG1] != ElfMagic1) {
			CommonMain::getTerminal()->error("ELF Header Magic 1 is incorrect!", "Elf Loader");

			return false;
		}

		if(elfHeader->elfIdentity[ElfIdent::ELF_ID_MAG2] != ElfMagic2) {
			CommonMain::getTerminal()->error("ELF Header Magic 2 is incorrect!", "Elf Loader");

			return false;
		}

		if(elfHeader->elfIdentity[ElfIdent::ELF_ID_MAG3] != ElfMagic3) {
			CommonMain::getTerminal()->error("ELF Header Magic 3 is incorrect!", "Elf Loader");

			return false;
		}

		return true;
	}

	bool Elf::isSupported(const ElfCommonHeader *elfHeader) {
		if(not isElf(elfHeader)) {
			CommonMain::getTerminal()->error("Invalid ELF file!", "Elf Loader");

			return false;
		}

		if(elfHeader->elfIdentity[ElfIdent::ELF_ID_DATA] != ElfLSB) {
			CommonMain::getTerminal()->error("Unsupported ELF File byte order!", "Elf Loader");

			return false;
		}

		if(elfHeader->elfMachine != ElfX86Machine) {
			CommonMain::getTerminal()->error("Unsupported ELF File target!", "Elf Loader");

			return false;
		}

		if(elfHeader->elfIdentity[ElfIdent::ELF_ID_VERSION] != ElfCurrVersion) {
			CommonMain::getTerminal()->error("Unsupported ELF File version!", "Elf Loader");

			return false;
		}

		if(elfHeader->elfType != ElfType::ET_REL and elfHeader->elfType != ElfType::ET_EXEC) {
			CommonMain::getTerminal()->error("Unsupported ELF File type!", "Elf Loader");

			return false;
		}

		return true;
	}

	bool Elf::is64Bit(const ElfCommonHeader *elfHeader) {
		if (elfHeader->elfIdentity[ElfIdent::ELF_ID_CLASS] != Elf64Bit) {
			CommonMain::getTerminal()->warn("ELF is not 64 bit!", "Elf Loader");

			return false;
		}

		return true;
	}

	bool Elf::is32Bit(const ElfCommonHeader *elfHeader) {
		if (elfHeader->elfIdentity[ElfIdent::ELF_ID_CLASS] != Elf32Bit) {
			CommonMain::getTerminal()->warn("ELF is not 32 bit!", "Elf Loader");

			return false;
		}

		return true;
	}

	Elf64SectionHeader *Elf::getElf64SectionHeader(const Elf64Header *elfHeader, const u16 index) {
		return &reinterpret_cast<Elf64SectionHeader *>(reinterpret_cast<u64>(elfHeader) + elfHeader->elfSectionHeaderOff)[index];
	}

	Elf32SectionHeader *Elf::getElf32SectionHeader(const Elf32Header *elfHeader, const u16 index) {
		return &reinterpret_cast<Elf32SectionHeader *>(reinterpret_cast<u64>(elfHeader) + elfHeader->elfSectionHeaderOff)[index];
	}

	char *Elf::elf64LookupString(Elf64Header *elfHeader, const u64 offset) {
		if(elfHeader->elfStringTableSectionHeaderIndex == ShnUndefined) {
			return nullptr;
		}

		return reinterpret_cast<char *>(elfHeader) + getElf64SectionHeader(elfHeader, elfHeader->elfStringTableSectionHeaderIndex)->offset + offset;
	}

	char *Elf::elf32LookupString(Elf32Header *elfHeader, const u64 offset) {
		if(elfHeader->elfStringTableSectionHeaderIndex == ShnUndefined) {
			return nullptr;
		}

		return reinterpret_cast<char *>(elfHeader) + getElf32SectionHeader(elfHeader, elfHeader->elfStringTableSectionHeaderIndex)->offset + offset;
	}

	u64 Elf::elf64GetSymValue(Elf64Header *elfHeader, u64 table, u64 idx) {

	}

	u64 Elf::elf32GetSymValue(Elf32Header *elfHeader, u64 table, u64 idx) {
		if(table == ShnUndefined || idx == ShnUndefined) {
			return 0;
		}

		Elf32SectionHeader *symtab = getElf32SectionHeader(elfHeader, table);

		uint32_t symTabEntries = symtab->size / symtab->entSize;

		if(idx >= symTabEntries) {
			CommonMain::getTerminal()->error("Symbol Index out of Range (%d:%u).", "Elf Loader", table, idx);

			return 2;
		}

		u64 symAddr = reinterpret_cast<u64>(elfHeader) + symtab->offset;
		Elf32SymbolTable *symbol = &reinterpret_cast<Elf32SymbolTable *>(symAddr)[idx];

		if(symbol->sectionIndex == ShnUndefined) {
			// External symbol, lookup value
			Elf32SectionHeader *strtab = getElf32SectionHeader(elfHeader, symtab->link);
			const char *name = reinterpret_cast<const char *>(elfHeader) + strtab->offset + symbol->name;

			extern u64 *elf_lookup_symbol(const char *name); // TODO: Make this

			u64 *target = elf_lookup_symbol(name);

			if(target == nullptr) {
				// Extern symbol not found
				if(ELF32_ST_BIND(symbol->info) & STB_WEAK) {
					// Weak symbol initialized as 0
					return 0;
				} else {
					CommonMain::getTerminal()->error("Undefined External Symbol: %s", "Elf Loader", name);

					return 2;
				}
			} else {
				return reinterpret_cast<u64>(target);
			}
		} else if(symbol->sectionIndex == ShnAbsolute) {
			// Absolute symbol
			return symbol->value;
		} else {
			// Internally defined symbol
			Elf32SectionHeader *target = getElf32SectionHeader(elfHeader, symbol->sectionIndex);
			return reinterpret_cast<u64>(elfHeader) + symbol->value + target->offset;
		}
	}
}