#include "Elf.hpp"

#include "CommonMain.hpp"
#include "Math.hpp"

namespace kernel::common::programs {
	// Helper function to map a range of memory for ELF loading
	void mapMemoryRange(Process *elfProc, AllocContext *ctx, const u64 baseAddr, const u64 size) {
		const u64 startPage = alignDown<u64>(baseAddr, pageSize);
		const u64 endPage = alignUp<u64>(baseAddr + size, pageSize);

		for (u64 addr = startPage; addr < endPage; addr += pageSize) {
			// Allocate a physical page
			const u64 *physPage = CommonMain::getInstance()->getPMM()->allocPages(1, false);

			if (physPage != nullptr) {
				// Map it to the virtual address
				// TODO: Actually set NX bit
				ctx->pageMap.mapPage(addr, reinterpret_cast<u64>(physPage), ctx->pageFlags | 0b100, false, false);

				if (addr > elfProc->topmostMappedPage) {
					elfProc->topmostMappedPage = addr;
				}
			} else {
				CommonMain::getTerminal()->error("Failed to allocate physical memory for ELF!", "Elf Loader");
			}
		}
	}

	u64 *Elf::loadElf(const u64 *elfFile, Process *elfProc, AllocContext *ctx, const u64 baseAddr) {
		const auto *elfHeader = reinterpret_cast<const ElfCommonHeader *>(elfFile);

		if (not isSupported(elfHeader)) {
			CommonMain::getTerminal()->error("ELF is not supported!", "Elf Loader");

			return nullptr;
		}

		switch (elfHeader->elfType) {
			case ElfType::ET_REL:
				return loadRel(elfFile, elfProc, ctx, baseAddr);

			case ElfType::ET_EXEC:
				return loadExe(elfFile, elfProc, ctx, 0); // TODO: Maybe baseAddr works

			case ElfType::ET_DYN:
				return loadExeDyn(elfFile, elfProc, ctx, baseAddr);

			default:
				CommonMain::getTerminal()->error("ELF type is not supported!", "Elf Loader");

				return nullptr;
		}
	}

	u64 *Elf::loadRel(const u64 *elfFile, Process *elfProc, AllocContext *ctx, const u64 baseAddr) {
		auto *elfHeader = reinterpret_cast<const ElfCommonHeader *>(elfFile);

		if (baseAddr == 0) {
			CommonMain::getTerminal()->error("Base address required for relocatable ELF!", "Elf Loader");

			return nullptr;
		}

		if (is64Bit(elfHeader)) {
			auto *elf64Header = reinterpret_cast<Elf64Header *>(const_cast<u64 *>(elfFile));

			// First pass: determine memory requirements and map memory
			u64 minAddr = ~0ULL;
			u64 maxAddr = 0;

			for (u16 i = 0; i < elf64Header->elfSectionHeaderAmount; i++) {
				const Elf64SectionHeader *shdr = getElf64SectionHeader(elf64Header, i);

				if (shdr->flags & SHF_ALLOC) {
					const u64 sectionStart = baseAddr + shdr->addr;
					const u64 sectionEnd = sectionStart + shdr->size;

					if (sectionStart < minAddr) {
						minAddr = sectionStart;
					}

					if (sectionEnd > maxAddr) {
						maxAddr = sectionEnd;
					}
				}
			}

			// Map the entire memory range needed
			if (maxAddr > minAddr) {
				mapMemoryRange(elfProc, ctx, minAddr, maxAddr - minAddr);
			}

			// Allocate sections
			for (u16 i = 0; i < elf64Header->elfSectionHeaderAmount; i++) {
				Elf64SectionHeader *shdr = getElf64SectionHeader(elf64Header, i);

				if (shdr->type == SHT_NOBITS) {
					// BSS section - allocate and zero
					if (shdr->flags & SHF_ALLOC) {
						auto *dest = reinterpret_cast<u8 *>(baseAddr + shdr->addr);

						memset(dest, 0, shdr->size);

						shdr->offset = baseAddr + shdr->addr - reinterpret_cast<u64>(elf64Header);
					}
				} else if (shdr->flags & SHF_ALLOC) {
					// Copy section to target address
					const u8 *src = reinterpret_cast<u8 *>(elf64Header) + shdr->offset;
					auto *dest = reinterpret_cast<u8 *>(baseAddr + shdr->addr);

					memcpy(dest, src, shdr->size);

					shdr->addr = baseAddr + shdr->addr;
				}
			}

			// Process relocations
			for (u16 i = 0; i < elf64Header->elfSectionHeaderAmount; i++) {
				const Elf64SectionHeader *shdr = getElf64SectionHeader(elf64Header, i);

				if (shdr->type == SHT_RELA) {
					// Process RELA relocations
					const auto *rela = reinterpret_cast<Elf64Rela *>(reinterpret_cast<u8 *>(elf64Header) + shdr->offset);
					const u64 relaCount = shdr->size / sizeof(Elf64Rela);

					for (u64 j = 0; j < relaCount; j++) {
						const u64 sym = ELF64_R_SYM(rela[j].info);
						const u64 type = ELF64_R_TYPE(rela[j].info);
						const u64 symVal = elf64GetSymValue(elf64Header, shdr->link, sym);

						auto *ref = reinterpret_cast<u64 *>(baseAddr + rela[j].offset);

						switch (type) {
							case R_X86_64_NONE:
								break;

							case R_X86_64_64:
								*ref = symVal + rela[j].addend;
								break;

							case R_X86_64_PC32:
								*reinterpret_cast<u32 *>(ref) = symVal + rela[j].addend - (baseAddr + rela[j].offset);
								break;

							case R_X86_64_32:
								*reinterpret_cast<u32 *>(ref) = symVal + rela[j].addend;
								break;

							case R_X86_64_32S:
								*reinterpret_cast<i32 *>(ref) = symVal + rela[j].addend;
								break;

							default:
								CommonMain::getTerminal()->error("Unsupported relocation type!", "Elf Loader");
								break;
						}
					}
				} else if (shdr->type == SHT_REL) {
					// Process REL relocations
					const auto *rel = reinterpret_cast<Elf64Rel *>(reinterpret_cast<u8 *>(elf64Header) + shdr->offset);
					const u64 relCount = shdr->size / sizeof(Elf64Rel);

					for (u64 j = 0; j < relCount; j++) {
						const u64 sym = ELF64_R_SYM(rel[j].info);
						const u64 type = ELF64_R_TYPE(rel[j].info);
						const u64 symVal = elf64GetSymValue(elf64Header, shdr->link, sym);

						auto *ref = reinterpret_cast<u64 *>(baseAddr + rel[j].offset);

						switch (type) {
							case R_X86_64_NONE:
								break;

							case R_X86_64_64:
								*ref = symVal + *ref;
								break;

							case R_X86_64_PC32:
								*reinterpret_cast<u32 *>(ref) = symVal + *reinterpret_cast<u32 *>(ref) - (baseAddr + rel[j].offset);
								break;

							case R_X86_64_32:
								*reinterpret_cast<u32 *>(ref) = symVal + *reinterpret_cast<u32 *>(ref);
								break;

							default:
								CommonMain::getTerminal()->error("Unsupported relocation type!", "Elf Loader");
								break;
						}
					}
				}
			}

			return reinterpret_cast<u64 *>(elf64Header->elfEntryAddr + baseAddr);
		}

		if (is32Bit(elfHeader)) {
			auto *elf32Header = reinterpret_cast<Elf32Header *>(const_cast<u64 *>(elfFile));

			// First pass: determine memory requirements and map memory
			u64 minAddr = ~0ULL;
			u64 maxAddr = 0;

			for (u16 i = 0; i < elf32Header->elfSectionHeaderAmount; i++) {
				const Elf32SectionHeader *shdr = getElf32SectionHeader(elf32Header, i);

				if (shdr->flags & SHF_ALLOC) {
					const u64 sectionStart = baseAddr + shdr->addr;
					const u64 sectionEnd = sectionStart + shdr->size;

					if (sectionStart < minAddr) {
						minAddr = sectionStart;
					}

					if (sectionEnd > maxAddr) {
						maxAddr = sectionEnd;
					}
				}
			}

			// Map the entire memory range needed
			if (maxAddr > minAddr) {
				mapMemoryRange(elfProc, ctx, minAddr, maxAddr - minAddr);
			}

			// Allocate sections
			for (u16 i = 0; i < elf32Header->elfSectionHeaderAmount; i++) {
				Elf32SectionHeader *shdr = getElf32SectionHeader(elf32Header, i);

				if (shdr->type == SHT_NOBITS) {
					// BSS section - allocate and zero
					if (shdr->flags & SHF_ALLOC) {
						auto *dest = reinterpret_cast<u8 *>(baseAddr + shdr->addr);

						memset(dest, 0, shdr->size);

						shdr->offset = baseAddr + shdr->addr - reinterpret_cast<u64>(elf32Header);
					}
				} else if (shdr->flags & SHF_ALLOC) {
					// Copy section to target address
					const u8 *src = reinterpret_cast<u8 *>(elf32Header) + shdr->offset;
					auto *dest = reinterpret_cast<u8 *>(baseAddr + shdr->addr);

					memcpy(dest, src, shdr->size);

					shdr->addr = baseAddr + shdr->addr;
				}
			}

			// Process relocations
			for (u16 i = 0; i < elf32Header->elfSectionHeaderAmount; i++) {
				const Elf32SectionHeader *shdr = getElf32SectionHeader(elf32Header, i);

				if (shdr->type == SHT_RELA) {
					// Process RELA relocations
					const auto *rela = reinterpret_cast<Elf32Rela *>(reinterpret_cast<u8 *>(elf32Header) + shdr->offset);
					const u32 relaCount = shdr->size / sizeof(Elf32Rela);

					for (u32 j = 0; j < relaCount; j++) {
						const u32 sym = ELF32_R_SYM(rela[j].info);
						const u32 type = ELF32_R_TYPE(rela[j].info);
						const u32 symVal = elf32GetSymValue(elf32Header, shdr->link, sym);

						auto *ref = reinterpret_cast<u32 *>(baseAddr + rela[j].offset);

						switch (type) {
							case R_386_NONE:
								break;

							case R_386_32:
								*ref = symVal + rela[j].addend;
								break;

							case R_386_PC32:
								*ref = symVal + rela[j].addend - (baseAddr + rela[j].offset);
								break;

							default:
								CommonMain::getTerminal()->error("Unsupported relocation type!", "Elf Loader");
								break;
						}
					}
				} else if (shdr->type == SHT_REL) {
					// Process REL relocations
					const auto *rel = reinterpret_cast<Elf32Rel *>(reinterpret_cast<u8 *>(elf32Header) + shdr->offset);
					const u32 relCount = shdr->size / sizeof(Elf32Rel);

					for (u32 j = 0; j < relCount; j++) {
						const u32 sym = ELF32_R_SYM(rel[j].info);
						const u32 type = ELF32_R_TYPE(rel[j].info);
						const u32 symVal = elf32GetSymValue(elf32Header, shdr->link, sym);

						auto *ref = reinterpret_cast<u32 *>(baseAddr + rel[j].offset);

						switch (type) {
							case R_386_NONE:
								break;

							case R_386_32:
								*ref = symVal + *ref;
								break;

							case R_386_PC32:
								*ref = symVal + *ref - (baseAddr + rel[j].offset);
								break;

							default:
								CommonMain::getTerminal()->error("Unsupported relocation type!", "Elf Loader");
								break;
						}
					}
				}
			}

			return reinterpret_cast<u64 *>(elf32Header->elfEntryAddr + baseAddr);
		}

		return nullptr;
	}

	u64 *Elf::loadExeDyn(const u64 *elfFile, Process *elfProc, AllocContext *ctx, const u64 baseAddr) {
		const auto *elfHeader = reinterpret_cast<const ElfCommonHeader *>(elfFile);

		if (is64Bit(elfHeader)) {
			auto *elf64Header = reinterpret_cast<Elf64Header *>(const_cast<u64 *>(elfFile));

			// Load program headers
			const auto *phdr = reinterpret_cast<Elf64ProgramHeader *>(reinterpret_cast<u8 *>(elf64Header) + elf64Header->elfProgHeaderOff);

			// Find the load base if not provided
			u64 loadBase = baseAddr;

			if (loadBase == 0) {
				// Find the lowest vaddr in loadable segments to determine offset
				u64 minVAddr = ~0ULL;

				for (u16 i = 0; i < elf64Header->elfProgHeaderAmount; i++) {
					if (phdr[i].type == PT_LOAD && phdr[i].vaddr < minVAddr) {
						minVAddr = phdr[i].vaddr;
					}
				}

				// TODO: Check why unused
				loadBase = pageSize; // No offset when not specified
			}

			const u64 offset = baseAddr;

			// First pass: map memory for all PT_LOAD segments
			for (u16 i = 0; i < elf64Header->elfProgHeaderAmount; i++) {
				if (phdr[i].type == PT_LOAD) {
					const u64 segmentStart = phdr[i].vaddr + offset;

					mapMemoryRange(elfProc, ctx, segmentStart, phdr[i].memsz);
				}
			}

			// Load all PT_LOAD segments
			for (u16 i = 0; i < elf64Header->elfProgHeaderAmount; i++) {
				if (phdr[i].type == PT_LOAD) {
					if (phdr[i].filesz > phdr[i].memsz) {
						CommonMain::getTerminal()->error("Invalid ELF segment: filesz > memsz!", "Elf Loader");

						return nullptr;
					}

					const u8 *src = reinterpret_cast<u8 *>(elf64Header) + phdr[i].offset;
					auto *dest = reinterpret_cast<u8 *>(phdr[i].vaddr + offset);

					// Copy file contents
					memcpy(dest, src, phdr[i].filesz);

					// Zero remaining memory (BSS)
					memset(dest + phdr[i].filesz, 0, phdr[i].memsz - phdr[i].filesz);
				}
			}

			// Process relocations if present
			for (u16 i = 0; i < elf64Header->elfSectionHeaderAmount; i++) {
				const Elf64SectionHeader *shdr = getElf64SectionHeader(elf64Header, i);

				if (shdr->type == SHT_RELA) {
					const auto *rela = reinterpret_cast<Elf64Rela *>(reinterpret_cast<u8 *>(elf64Header) + shdr->offset);
					const u64 relaCount = shdr->size / sizeof(Elf64Rela);

					for (u64 j = 0; j < relaCount; j++) {
						const u64 type = ELF64_R_TYPE(rela[j].info);
						auto *ref = reinterpret_cast<u64 *>(rela[j].offset + offset);

						switch (type) {
							case R_X86_64_RELATIVE:
								*ref = offset + rela[j].addend;
								break;

							case R_X86_64_64:
								{
									const u64 sym = ELF64_R_SYM(rela[j].info);
									const u64 symVal = elf64GetSymValue(elf64Header, shdr->link, sym);
									*ref = symVal + rela[j].addend + offset;
								}

								break;
							default:
								// Other relocations handled dynamically
								break;
						}
					}
				}
			}

			return reinterpret_cast<u64 *>(elf64Header->elfEntryAddr + offset);
		}

		if (is32Bit(elfHeader)) {
			auto *elf32Header = reinterpret_cast<Elf32Header *>(const_cast<u64 *>(elfFile));

			// Load program headers
			const auto *phdr = reinterpret_cast<Elf32ProgramHeader *>(reinterpret_cast<u8 *>(elf32Header) + elf32Header->elfProgHeaderOff);

			// Find the load base if not provided
			u32 loadBase = static_cast<u32>(baseAddr);

			if (loadBase == 0) {
				u32 minVAddr = ~0U;

				for (u16 i = 0; i < elf32Header->elfProgHeaderAmount; i++) {
					if (phdr[i].type == PT_LOAD && phdr[i].vaddr < minVAddr) {
						minVAddr = phdr[i].vaddr;
					}
				}

				loadBase = minVAddr;
			}

			const u32 offset = loadBase;

			// First pass: map memory for all PT_LOAD segments
			for (u16 i = 0; i < elf32Header->elfProgHeaderAmount; i++) {
				if (phdr[i].type == PT_LOAD) {
					const u64 segmentStart = phdr[i].vaddr + offset;

					mapMemoryRange(elfProc, ctx, segmentStart, phdr[i].memsz);
				}
			}

			// Load all PT_LOAD segments
			for (u16 i = 0; i < elf32Header->elfProgHeaderAmount; i++) {
				if (phdr[i].type == PT_LOAD) {
					if (phdr[i].filesz > phdr[i].memsz) {
						CommonMain::getTerminal()->error("Invalid ELF segment: filesz > memsz!", "Elf Loader");
						return nullptr;
					}

					const u8 *src = reinterpret_cast<u8 *>(elf32Header) + phdr[i].offset;
					auto *dest = reinterpret_cast<u8 *>(static_cast<u64>(phdr[i].vaddr + offset));

					// Copy file contents
					memcpy(dest, src, phdr[i].filesz);

					// Zero remaining memory (BSS)
					memset(dest + phdr[i].filesz, 0, phdr[i].memsz - phdr[i].filesz);
				}
			}

			// Process relocations if present
			for (u16 i = 0; i < elf32Header->elfSectionHeaderAmount; i++) {
				const Elf32SectionHeader *shdr = getElf32SectionHeader(elf32Header, i);

				if (shdr->type == SHT_RELA) {
					const auto *rela = reinterpret_cast<Elf32Rela *>(reinterpret_cast<u8 *>(elf32Header) + shdr->offset);
					const u32 relaCount = shdr->size / sizeof(Elf32Rela);

					for (u32 j = 0; j < relaCount; j++) {
						const u32 type = ELF32_R_TYPE(rela[j].info);
						auto *ref = reinterpret_cast<u32 *>(static_cast<u64>(rela[j].offset + offset));

						switch (type) {
							case R_386_RELATIVE:
								*ref = offset + rela[j].addend;
								break;

							case R_386_32:
								{
									const u32 sym = ELF32_R_SYM(rela[j].info);
									const u32 symVal = elf32GetSymValue(elf32Header, shdr->link, sym);
									*ref = symVal + rela[j].addend + offset;
								}
								break;

							default:
								break;
						}
					}
				} else if (shdr->type == SHT_REL) {
					const auto *rel = reinterpret_cast<Elf32Rel *>(reinterpret_cast<u8 *>(elf32Header) + shdr->offset);
					const u32 relCount = shdr->size / sizeof(Elf32Rel);

					for (u32 j = 0; j < relCount; j++) {
						const u32 type = ELF32_R_TYPE(rel[j].info);
						auto *ref = reinterpret_cast<u32 *>(static_cast<u64>(rel[j].offset + offset));

						switch (type) {
							case R_386_RELATIVE:
								*ref = *ref + offset;
								break;

							default:
								break;
						}
					}
				}
			}

			return reinterpret_cast<u64 *>(static_cast<u64>(elf32Header->elfEntryAddr + offset));
		}

		return nullptr;
	}

	u64 *Elf::loadExe(const u64 *elfFile, Process *elfProc, AllocContext *ctx, const u64 baseAddr) {
		const auto *elfHeader = reinterpret_cast<const ElfCommonHeader *>(elfFile);

		if (is64Bit(elfHeader)) {
			auto *elf64Header = reinterpret_cast<Elf64Header *>(const_cast<u64 *>(elfFile));

			// Load program headers
			const auto *phdr = reinterpret_cast<Elf64ProgramHeader *>(reinterpret_cast<u8 *>(elf64Header) + elf64Header->elfProgHeaderOff);

			// For non-relocatable executables, we load at their specified addresses
			// If baseAddr is provided, it's used as an offset
			const u64 offset = baseAddr;

			// First pass: map memory for all PT_LOAD segments
			for (u16 i = 0; i < elf64Header->elfProgHeaderAmount; i++) {
				if (phdr[i].type == PT_LOAD) {
					const u64 segmentStart = phdr[i].vaddr + offset;

					mapMemoryRange(elfProc, ctx, segmentStart, phdr[i].memsz);
				}
			}

			// Load all PT_LOAD segments at their specified virtual addresses
			for (u16 i = 0; i < elf64Header->elfProgHeaderAmount; i++) {
				if (phdr[i].type == PT_LOAD) {
					if (phdr[i].filesz > phdr[i].memsz) {
						CommonMain::getTerminal()->error("Invalid ELF segment: filesz > memsz!", "Elf Loader");

						return nullptr;
					}

					const u8 *src = reinterpret_cast<u8 *>(elf64Header) + phdr[i].offset;
					auto *dest = reinterpret_cast<u8 *>(phdr[i].vaddr + offset);

					// Copy file contents
					memcpy(dest, src, phdr[i].filesz);

					// Zero remaining memory (BSS)
					memset(dest + phdr[i].filesz, 0, phdr[i].memsz - phdr[i].filesz);
				}
			}

			return reinterpret_cast<u64 *>(elf64Header->elfEntryAddr + offset);
		}

		if (is32Bit(elfHeader)) {
			auto *elf32Header = reinterpret_cast<Elf32Header *>(const_cast<u64 *>(elfFile));

			// Load program headers
			const auto *phdr = reinterpret_cast<Elf32ProgramHeader *>(reinterpret_cast<u8 *>(elf32Header) + elf32Header->elfProgHeaderOff);

			const u32 offset = static_cast<u32>(baseAddr);

			// First pass: map memory for all PT_LOAD segments
			for (u16 i = 0; i < elf32Header->elfProgHeaderAmount; i++) {
				if (phdr[i].type == PT_LOAD) {
					const u64 segmentStart = phdr[i].vaddr + offset;

					mapMemoryRange(elfProc, ctx, segmentStart, phdr[i].memsz);
				}
			}

			// Load all PT_LOAD segments at their specified virtual addresses
			for (u16 i = 0; i < elf32Header->elfProgHeaderAmount; i++) {
				if (phdr[i].type == PT_LOAD) {
					if (phdr[i].filesz > phdr[i].memsz) {
						CommonMain::getTerminal()->error("Invalid ELF segment: filesz > memsz!", "Elf Loader");
						return nullptr;
					}

					const u8 *src = reinterpret_cast<u8 *>(elf32Header) + phdr[i].offset;
					auto *dest = reinterpret_cast<u8 *>(static_cast<u64>(phdr[i].vaddr + offset));

					// Copy file contents
					memcpy(dest, src, phdr[i].filesz);

					// Zero remaining memory (BSS)
					memset(dest + phdr[i].filesz, 0, phdr[i].memsz - phdr[i].filesz);
				}
			}

			return reinterpret_cast<u64 *>(static_cast<u64>(elf32Header->elfEntryAddr + offset));
		}

		return nullptr;
	}

	bool Elf::isElf(const ElfCommonHeader *elfHeader) {
		if (not elfHeader) {
			return false;
		}

		if (elfHeader->elfIdentity[ElfIdent::ELF_ID_MAG0] != ElfMagic0) {
			CommonMain::getTerminal()->error("ELF Header Magic 0 is incorrect!", "Elf Loader");

			return false;
		}

		if (elfHeader->elfIdentity[ElfIdent::ELF_ID_MAG1] != ElfMagic1) {
			CommonMain::getTerminal()->error("ELF Header Magic 1 is incorrect!", "Elf Loader");

			return false;
		}

		if (elfHeader->elfIdentity[ElfIdent::ELF_ID_MAG2] != ElfMagic2) {
			CommonMain::getTerminal()->error("ELF Header Magic 2 is incorrect!", "Elf Loader");

			return false;
		}

		if (elfHeader->elfIdentity[ElfIdent::ELF_ID_MAG3] != ElfMagic3) {
			CommonMain::getTerminal()->error("ELF Header Magic 3 is incorrect!", "Elf Loader");

			return false;
		}

		return true;
	}

	bool Elf::isSupported(const ElfCommonHeader *elfHeader) {
		if (not isElf(elfHeader)) {
			CommonMain::getTerminal()->error("Invalid ELF file!", "Elf Loader");

			return false;
		}

		if (elfHeader->elfIdentity[ElfIdent::ELF_ID_DATA] != ElfLSB) {
			CommonMain::getTerminal()->error("Unsupported ELF File byte order!", "Elf Loader");

			return false;
		}

		if (elfHeader->elfMachine != ElfX86Machine) {
			CommonMain::getTerminal()->error("Unsupported ELF File target: %lu", "Elf Loader", elfHeader->elfMachine);

			return false;
		}

		if (elfHeader->elfIdentity[ElfIdent::ELF_ID_VERSION] != ElfCurrVersion) {
			CommonMain::getTerminal()->error("Unsupported ELF File version!", "Elf Loader");

			return false;
		}

		if (elfHeader->elfType != ElfType::ET_REL and elfHeader->elfType != ElfType::ET_EXEC and elfHeader->elfType != ElfType::ET_DYN) {
			CommonMain::getTerminal()->error("Unsupported ELF File type: %lu", "Elf Loader", elfHeader->elfType);

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

	u64 Elf::elf64GetSymValue(Elf64Header *elfHeader, const u64 table, const u64 idx) {
		if (table == ShnUndefined || idx == ShnUndefined) {
			return 0;
		}

		const Elf64SectionHeader *symtab = getElf64SectionHeader(elfHeader, table);

		u64 symtabEntries = symtab->size / sizeof(Elf64SymbolTable);

		if (idx >= symtabEntries) {
			CommonMain::getTerminal()->error("Symbol index out of bounds!", "Elf Loader");

			return 0;
		}

		const Elf64SymbolTable *symbol = &(reinterpret_cast<Elf64SymbolTable *>(
			reinterpret_cast<u8 *>(elfHeader) + symtab->offset
		))[idx];

		if (symbol->sectionIndex == ShnUndefined) {
			// External symbol - would need dynamic linking
			CommonMain::getTerminal()->error("Undefined symbol!", "Elf Loader");

			return 0;
		}

		if (symbol->sectionIndex == ShnAbsolute || symbol->sectionIndex == ShnCommon) {
			// Absolute or common symbol
			return symbol->value;
		}

		// Symbol relative to a section
		const Elf64SectionHeader *target = getElf64SectionHeader(elfHeader, symbol->sectionIndex);

		return target->addr + symbol->value;
	}

	u64 Elf::elf32GetSymValue(Elf32Header *elfHeader, const u64 table, const u64 idx) {
		if (table == ShnUndefined || idx == ShnUndefined) {
			return 0;
		}

		const Elf32SectionHeader *symtab = getElf32SectionHeader(elfHeader, table);

		u32 symtabEntries = symtab->size / sizeof(Elf32SymbolTable);

		if (idx >= symtabEntries) {
			CommonMain::getTerminal()->error("Symbol index out of bounds!", "Elf Loader");

			return 0;
		}

		Elf32SymbolTable *symbol = &(reinterpret_cast<Elf32SymbolTable *>(
			reinterpret_cast<u8 *>(elfHeader) + symtab->offset
		))[idx];

		if (symbol->sectionIndex == ShnUndefined) {
			// External symbol - would need dynamic linking
			CommonMain::getTerminal()->error("Undefined symbol!", "Elf Loader");

			return 0;
		}

		if (symbol->sectionIndex == ShnAbsolute || symbol->sectionIndex == ShnCommon) {
			// Absolute or common symbol
			return symbol->value;
		}

		// Symbol relative to a section
		const Elf32SectionHeader *target = getElf32SectionHeader(elfHeader, symbol->sectionIndex);

		return target->addr + symbol->value;
	}
}
