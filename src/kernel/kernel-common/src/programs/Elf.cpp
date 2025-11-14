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
}