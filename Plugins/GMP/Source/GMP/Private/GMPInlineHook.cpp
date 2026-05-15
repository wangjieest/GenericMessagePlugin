// Copyright GMP, Inc. All Rights Reserved.
// Minimal cross-platform inline hook: x64 + arm64
// Based on emock (Apache 2.0) core hooking technique with arm64 additions.

#include "GMPInlineHook.h"

#if WITH_EDITOR

#include "HAL/PlatformMemory.h"

#if !PLATFORM_WINDOWS && !PLATFORM_MAC && !PLATFORM_LINUX
#error "GMPInlineHook: unsupported platform"
#endif

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#else
#include <sys/mman.h>
#include <unistd.h>
#if PLATFORM_MAC
#include <mach/mach_vm.h>
#include <mach/mach_init.h>
#include <libkern/OSCacheControl.h>
#endif
#endif

namespace GMPHook
{
namespace
{
	static constexpr SIZE_T kPageSize = 4096;
	static constexpr SIZE_T kMaxDelta = 0x7FFF0000ull; // ~2GB for x64 rel32
	static constexpr SIZE_T kTrampolineSlotSize = 64;

	// --- Memory protection ---

	bool SetMemoryRWX(void* Addr, SIZE_T Size)
	{
#if PLATFORM_WINDOWS
		DWORD OldProtect;
		return VirtualProtect(Addr, Size, PAGE_EXECUTE_READWRITE, &OldProtect) != 0;
#else
		void* PageAddr = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(Addr) & ~(uintptr_t)(kPageSize - 1));
		SIZE_T Len = (reinterpret_cast<uint8*>(Addr) + Size) - reinterpret_cast<uint8*>(PageAddr);
		return mprotect(PageAddr, Len, PROT_READ | PROT_WRITE | PROT_EXEC) == 0;
#endif
	}

	void FlushInstructionCache(void* Addr, SIZE_T Size)
	{
#if PLATFORM_WINDOWS
		::FlushInstructionCache(GetCurrentProcess(), Addr, Size);
#elif PLATFORM_MAC && PLATFORM_CPU_ARM_FAMILY
		sys_icache_invalidate(Addr, Size);
#endif
	}

	// --- Trampoline allocator (within ±2GB of target on x64) ---

	struct FTrampolineBlock
	{
		uint8* Base;
		SIZE_T Used;
		SIZE_T Capacity;
	};

	static TArray<FTrampolineBlock> GTrampolineBlocks;

	uint8* AllocateNearby(const void* Target, SIZE_T Size)
	{
#if PLATFORM_CPU_X86_FAMILY
		// x64: need within ±2GB
		for (auto& Block : GTrampolineBlocks)
		{
			intptr_t Delta = reinterpret_cast<intptr_t>(Block.Base) - reinterpret_cast<intptr_t>(Target);
			if (FMath::Abs(Delta) < (intptr_t)kMaxDelta && Block.Used + Size <= Block.Capacity)
			{
				uint8* Slot = Block.Base + Block.Used;
				Block.Used = Align(Block.Used + Size, kTrampolineSlotSize);
				return Slot;
			}
		}

		// Allocate new block near target
		uint8* Alloc = nullptr;
#if PLATFORM_WINDOWS
		const uint8* SearchBase = reinterpret_cast<const uint8*>(Target);
		MEMORY_BASIC_INFORMATION Mbi = {};
		for (const uint8* Addr = SearchBase > reinterpret_cast<const uint8*>(kMaxDelta) ? SearchBase - kMaxDelta : nullptr;
			 Addr < SearchBase + kMaxDelta;
			 Addr += Mbi.RegionSize)
		{
			if (!VirtualQuery(Addr, &Mbi, sizeof(Mbi)))
				break;
			if (Mbi.RegionSize == 0)
				break;
			if (Mbi.State != MEM_FREE)
				continue;
			Alloc = reinterpret_cast<uint8*>(VirtualAlloc(Mbi.AllocationBase, kPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
			if (Alloc)
			{
				intptr_t D = reinterpret_cast<intptr_t>(Alloc) - reinterpret_cast<intptr_t>(Target);
				if (FMath::Abs(D) < (intptr_t)kMaxDelta)
					break;
				VirtualFree(Alloc, 0, MEM_RELEASE);
				Alloc = nullptr;
			}
		}
#else // POSIX
#if PLATFORM_MAC
		// macOS: search for free region via mach_vm_region_recurse
		uintptr_t LastEnd = FMath::Max<uintptr_t>(reinterpret_cast<uintptr_t>(Target) - kMaxDelta, kPageSize);
		while (true)
		{
			mach_vm_address_t Address = LastEnd;
			mach_vm_size_t RegionSize = 0;
			uint32 Depth = 2048;
			vm_region_submap_info_data_64_t Info;
			mach_msg_type_number_t Count = VM_REGION_SUBMAP_INFO_COUNT_64;
			kern_return_t Kr = mach_vm_region_recurse(mach_task_self(), &Address, &RegionSize, &Depth, (vm_region_recurse_info_t)&Info, &Count);
			if (Kr != KERN_SUCCESS)
				break;

			if (LastEnd && Address > LastEnd && Address - LastEnd >= kPageSize)
			{
				uintptr_t Gap = Align(LastEnd, kPageSize);
				intptr_t D = (intptr_t)Gap - reinterpret_cast<intptr_t>(Target);
				if (FMath::Abs(D) < (intptr_t)kMaxDelta)
				{
					void* M = mmap(reinterpret_cast<void*>(Gap), kPageSize, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
					if (M != MAP_FAILED)
					{
						Alloc = reinterpret_cast<uint8*>(M);
						break;
					}
				}
			}
			LastEnd = Address + RegionSize;
			if ((intptr_t)LastEnd - reinterpret_cast<intptr_t>(Target) > (intptr_t)kMaxDelta)
				break;
		}
#else // Linux
		uintptr_t Base = FMath::Max<uintptr_t>(reinterpret_cast<uintptr_t>(Target) - kMaxDelta, kPageSize);
		Base = Align(Base, kPageSize);
		for (uintptr_t Addr = Base; Addr < reinterpret_cast<uintptr_t>(Target) + kMaxDelta; Addr += kPageSize)
		{
			void* M = mmap(reinterpret_cast<void*>(Addr), kPageSize, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
			if (M != MAP_FAILED)
			{
				Alloc = reinterpret_cast<uint8*>(M);
				break;
			}
		}
#endif
#endif
		if (Alloc)
		{
			FTrampolineBlock Block;
			Block.Base = Alloc;
			Block.Used = Align(Size, kTrampolineSlotSize);
			Block.Capacity = kPageSize;
			GTrampolineBlocks.Add(Block);
			return Alloc;
		}
		return nullptr;

#elif PLATFORM_CPU_ARM_FAMILY
		// arm64: ADRP range is ±4GB, B/BL ±128MB. We use absolute branch so no range limit.
		// Just allocate a page anywhere.
		uint8* Alloc = nullptr;
#if PLATFORM_WINDOWS
		Alloc = reinterpret_cast<uint8*>(VirtualAlloc(nullptr, kPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
#else
		void* M = mmap(nullptr, kPageSize, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (M != MAP_FAILED)
			Alloc = reinterpret_cast<uint8*>(M);
#endif
		if (Alloc)
		{
			FTrampolineBlock Block;
			Block.Base = Alloc;
			Block.Used = Align(Size, kTrampolineSlotSize);
			Block.Capacity = kPageSize;
			GTrampolineBlocks.Add(Block);
			return Alloc;
		}
		return nullptr;
#else
#error "Unsupported CPU"
#endif
	}

	// --- Hook entry storage ---

	static TArray<FHookEntry> GHookEntries;

	FHookEntry* FindEntry(void* Target)
	{
		for (auto& E : GHookEntries)
		{
			if (E.Target == Target)
				return &E;
		}
		return nullptr;
	}

#if PLATFORM_CPU_X86_FAMILY
	// --- Minimal x64 instruction length decoder ---
	// Handles common function prologue instructions. Returns 0 on unknown instruction.

	uint32 X64InstructionLength(const uint8* Code)
	{
		const uint8* P = Code;

		// REX prefix (0x40-0x4F)
		bool bHasREX = false;
		uint8 REX = 0;
		if ((*P & 0xF0) == 0x40)
		{
			REX = *P;
			bHasREX = true;
			++P;
		}

		uint8 Op = *P++;

		// Single-byte simple instructions
		// NOP (0x90), PUSH r64 (0x50-0x57), POP r64 (0x58-0x5F)
		if (Op == 0x90 || (Op >= 0x50 && Op <= 0x5F))
			return (uint32)(P - Code);
		// RET (0xC3)
		if (Op == 0xC3)
			return (uint32)(P - Code);
		// INT3 (0xCC)
		if (Op == 0xCC)
			return (uint32)(P - Code);

		// MOV r/m64, r64 (0x89) or MOV r64, r/m64 (0x8B) — has ModR/M
		// SUB r/m64, imm8 (0x83 /5) or ADD, AND, OR, XOR, CMP
		// LEA r64, m (0x8D) — has ModR/M
		// TEST r/m, r (0x85)
		if (Op == 0x89 || Op == 0x8B || Op == 0x8D || Op == 0x85 || Op == 0x87 || Op == 0x31 || Op == 0x33 || Op == 0x29 || Op == 0x01 || Op == 0x39 || Op == 0x3B)
		{
			// ModR/M byte
			uint8 ModRM = *P++;
			uint8 Mod = (ModRM >> 6) & 3;
			uint8 RM = ModRM & 7;
			if (Mod == 0 && RM == 5) P += 4; // [RIP+disp32]
			else if (Mod == 0 && RM == 4) { uint8 SIB = *P++; if ((SIB & 7) == 5) P += 4; } // SIB + disp32
			else if (Mod == 1) { if (RM == 4) P++; P++; } // SIB? + disp8
			else if (Mod == 2) { if (RM == 4) P++; P += 4; } // SIB? + disp32
			// Mod==3 is register-register, no extra bytes
			return (uint32)(P - Code);
		}

		// 0x83: op r/m, imm8 (ADD/OR/ADC/SBB/AND/SUB/XOR/CMP)
		// 0x81: op r/m, imm32
		if (Op == 0x83 || Op == 0x81 || Op == 0x80)
		{
			uint8 ModRM = *P++;
			uint8 Mod = (ModRM >> 6) & 3;
			uint8 RM = ModRM & 7;
			if (Mod == 0 && RM == 5) P += 4;
			else if (Mod == 0 && RM == 4) { uint8 SIB = *P++; if ((SIB & 7) == 5) P += 4; }
			else if (Mod == 1) { if (RM == 4) P++; P++; }
			else if (Mod == 2) { if (RM == 4) P++; P += 4; }
			P += (Op == 0x81) ? 4 : 1; // imm32 or imm8
			return (uint32)(P - Code);
		}

		// MOV r64, imm64 (0xB8-0xBF with REX.W)
		if (Op >= 0xB8 && Op <= 0xBF)
		{
			P += (bHasREX && (REX & 0x08)) ? 8 : 4; // REX.W → imm64, else imm32
			return (uint32)(P - Code);
		}

		// MOV r/m8, imm8 (0xC6) or MOV r/m, imm32 (0xC7)
		if (Op == 0xC6 || Op == 0xC7)
		{
			uint8 ModRM = *P++;
			uint8 Mod = (ModRM >> 6) & 3;
			uint8 RM = ModRM & 7;
			if (Mod == 0 && RM == 5) P += 4;
			else if (Mod == 0 && RM == 4) { uint8 SIB = *P++; if ((SIB & 7) == 5) P += 4; }
			else if (Mod == 1) { if (RM == 4) P++; P++; }
			else if (Mod == 2) { if (RM == 4) P++; P += 4; }
			P += (Op == 0xC7) ? 4 : 1;
			return (uint32)(P - Code);
		}

		// CALL rel32 (0xE8), JMP rel32 (0xE9)
		if (Op == 0xE8 || Op == 0xE9)
		{
			P += 4;
			return (uint32)(P - Code);
		}

		// JMP/Jcc short (0xEB, 0x70-0x7F)
		if (Op == 0xEB || (Op >= 0x70 && Op <= 0x7F))
		{
			P += 1;
			return (uint32)(P - Code);
		}

		// SUB rsp, imm8 via 0x83 already handled
		// Two-byte opcodes (0x0F prefix)
		if (Op == 0x0F)
		{
			uint8 Op2 = *P++;
			// Jcc near (0x80-0x8F + 4 byte offset)
			if (Op2 >= 0x80 && Op2 <= 0x8F)
			{
				P += 4;
				return (uint32)(P - Code);
			}
			// MOVAPS/MOVUPS etc with ModR/M
			if ((Op2 >= 0x10 && Op2 <= 0x17) || Op2 == 0x28 || Op2 == 0x29 || Op2 == 0x2E || Op2 == 0x2F ||
				Op2 == 0x1F || Op2 == 0xB6 || Op2 == 0xB7 || Op2 == 0xBE || Op2 == 0xBF ||
				Op2 == 0xAF || Op2 == 0xA3 || Op2 == 0xAB || Op2 == 0xB3)
			{
				uint8 ModRM = *P++;
				uint8 Mod = (ModRM >> 6) & 3;
				uint8 RM = ModRM & 7;
				if (Mod == 0 && RM == 5) P += 4;
				else if (Mod == 0 && RM == 4) { uint8 SIB = *P++; if ((SIB & 7) == 5) P += 4; }
				else if (Mod == 1) { if (RM == 4) P++; P++; }
				else if (Mod == 2) { if (RM == 4) P++; P += 4; }
				return (uint32)(P - Code);
			}
		}

		// ENDBR64 (F3 0F 1E FA) — may appear before REX
		if (!bHasREX && Op == 0xF3)
		{
			if (P[0] == 0x0F && P[1] == 0x1E && P[2] == 0xFA)
			{
				P += 3;
				return (uint32)(P - Code);
			}
		}

		return 0; // Unknown — caller should handle
	}

	// Find minimum N bytes covering complete instructions
	uint32 FindInstructionBoundary(const uint8* Code, uint32 MinBytes)
	{
		uint32 Total = 0;
		while (Total < MinBytes)
		{
			uint32 Len = X64InstructionLength(Code + Total);
			if (Len == 0)
				return 0; // Unknown instruction
			Total += Len;
		}
		return Total;
	}

	// Relocate x64 instructions: fix RIP-relative addressing
	void RelocateX64(uint8* Dst, const uint8* Src, uint32 Size)
	{
		FMemory::Memcpy(Dst, Src, Size);
		// Scan for RIP-relative (ModRM with Mod=00, R/M=101)
		uint32 Offset = 0;
		while (Offset < Size)
		{
			uint8* P = Dst + Offset;
			const uint8* S = Src + Offset;
			uint32 Len = X64InstructionLength(S);
			if (Len == 0)
				break;

			// Check for RIP-relative: any instruction with ModRM byte where Mod=00, RM=5
			// Simple heuristic: scan for [RIP+disp32] pattern and fix displacement
			uint8* ModRMPtr = nullptr;
			uint32 PrefixLen = 0;
			const uint8* Scan = S;
			if ((*Scan & 0xF0) == 0x40) { PrefixLen++; Scan++; } // REX
			uint8 Opcode = *Scan++;
			PrefixLen++;

			bool bHasModRM = false;
			if (Opcode == 0x0F) { Opcode = *Scan++; PrefixLen++; bHasModRM = true; }
			else if ((Opcode >= 0x80 && Opcode <= 0x8D) || Opcode == 0x85 || Opcode == 0x87 ||
					 Opcode == 0x89 || Opcode == 0x8B || Opcode == 0xC6 || Opcode == 0xC7 ||
					 Opcode == 0x29 || Opcode == 0x01 || Opcode == 0x31 || Opcode == 0x33 ||
					 Opcode == 0x39 || Opcode == 0x3B)
				bHasModRM = true;

			if (bHasModRM && PrefixLen < Len)
			{
				uint8 ModRM = Dst[Offset + PrefixLen];
				uint8 Mod = (ModRM >> 6) & 3;
				uint8 RM = ModRM & 7;
				if (Mod == 0 && RM == 5)
				{
					// RIP-relative: fix disp32
					int32* Disp = reinterpret_cast<int32*>(&Dst[Offset + PrefixLen + 1]);
					intptr_t OrigAddr = reinterpret_cast<intptr_t>(S) + Len + *Disp;
					*Disp = (int32)(OrigAddr - (reinterpret_cast<intptr_t>(P) + Len));
				}
			}

			// Fix CALL/JMP rel32
			if (Opcode == 0xE8 || Opcode == 0xE9)
			{
				int32* Rel = reinterpret_cast<int32*>(P + PrefixLen);
				intptr_t AbsTarget = reinterpret_cast<intptr_t>(S + Len) + *Rel;
				*Rel = (int32)(AbsTarget - reinterpret_cast<intptr_t>(P + Len));
			}

			Offset += Len;
		}
	}

#elif PLATFORM_CPU_ARM_FAMILY

	// --- arm64 instruction relocation ---

	static constexpr uint32 kARM64HookSize = 16; // LDR X16, #8; BR X16; .quad addr

	void WriteAbsoluteBranch(uint8* Dst, uintptr_t Target)
	{
		// LDR X16, #8  (0x58000050)
		// BR X16       (0xD61F0200)
		// .quad Target
		uint32* Code = reinterpret_cast<uint32*>(Dst);
		Code[0] = 0x58000050; // LDR X16, PC+8
		Code[1] = 0xD61F0200; // BR X16
		*reinterpret_cast<uint64*>(&Code[2]) = Target;
	}

	// Relocate a single arm64 instruction from Src to Dst
	// Returns true if successfully relocated
	bool RelocateARM64Instruction(uint32* Dst, const uint32* Src, intptr_t Delta)
	{
		uint32 Insn = *Src;
		*Dst = Insn; // default: copy as-is

		// B/BL: imm26 (bits 25:0), PC-relative ±128MB
		if ((Insn & 0x7C000000) == 0x14000000)
		{
			int32 Imm26 = (int32)(Insn & 0x03FFFFFF);
			if (Imm26 & 0x02000000) Imm26 |= (int32)0xFC000000; // sign extend
			intptr_t AbsTarget = reinterpret_cast<intptr_t>(Src) + (intptr_t)Imm26 * 4;
			intptr_t NewOff = AbsTarget - reinterpret_cast<intptr_t>(Dst);
			int32 NewImm = (int32)(NewOff / 4);
			if (NewImm < -(1 << 25) || NewImm >= (1 << 25))
				return false; // out of range
			*Dst = (Insn & 0xFC000000) | (NewImm & 0x03FFFFFF);
			return true;
		}

		// B.cond: imm19 (bits 23:5), PC-relative ±1MB
		if ((Insn & 0xFF000010) == 0x54000000)
		{
			int32 Imm19 = (int32)((Insn >> 5) & 0x7FFFF);
			if (Imm19 & 0x40000) Imm19 |= (int32)0xFFF80000;
			intptr_t AbsTarget = reinterpret_cast<intptr_t>(Src) + (intptr_t)Imm19 * 4;
			intptr_t NewOff = AbsTarget - reinterpret_cast<intptr_t>(Dst);
			int32 NewImm = (int32)(NewOff / 4);
			if (NewImm < -(1 << 18) || NewImm >= (1 << 18))
				return false;
			*Dst = (Insn & ~(0x7FFFF << 5)) | ((NewImm & 0x7FFFF) << 5);
			return true;
		}

		// CBZ/CBNZ: imm19 (bits 23:5)
		if ((Insn & 0x7E000000) == 0x34000000)
		{
			int32 Imm19 = (int32)((Insn >> 5) & 0x7FFFF);
			if (Imm19 & 0x40000) Imm19 |= (int32)0xFFF80000;
			intptr_t AbsTarget = reinterpret_cast<intptr_t>(Src) + (intptr_t)Imm19 * 4;
			intptr_t NewOff = AbsTarget - reinterpret_cast<intptr_t>(Dst);
			int32 NewImm = (int32)(NewOff / 4);
			if (NewImm < -(1 << 18) || NewImm >= (1 << 18))
				return false;
			*Dst = (Insn & ~(0x7FFFF << 5)) | ((NewImm & 0x7FFFF) << 5);
			return true;
		}

		// TBZ/TBNZ: imm14 (bits 18:5)
		if ((Insn & 0x7E000000) == 0x36000000)
		{
			int32 Imm14 = (int32)((Insn >> 5) & 0x3FFF);
			if (Imm14 & 0x2000) Imm14 |= (int32)0xFFFFC000;
			intptr_t AbsTarget = reinterpret_cast<intptr_t>(Src) + (intptr_t)Imm14 * 4;
			intptr_t NewOff = AbsTarget - reinterpret_cast<intptr_t>(Dst);
			int32 NewImm = (int32)(NewOff / 4);
			if (NewImm < -(1 << 13) || NewImm >= (1 << 13))
				return false;
			*Dst = (Insn & ~(0x3FFF << 5)) | ((NewImm & 0x3FFF) << 5);
			return true;
		}

		// ADR: imm21, PC-relative
		if ((Insn & 0x9F000000) == 0x10000000)
		{
			int32 ImmLo = (Insn >> 29) & 3;
			int32 ImmHi = (int32)((Insn >> 5) & 0x7FFFF);
			if (ImmHi & 0x40000) ImmHi |= (int32)0xFFF80000;
			int32 Imm21 = (ImmHi << 2) | ImmLo;
			intptr_t AbsTarget = reinterpret_cast<intptr_t>(Src) + Imm21;
			intptr_t NewOff = AbsTarget - reinterpret_cast<intptr_t>(Dst);
			if (NewOff < -(1 << 20) || NewOff >= (1 << 20))
				return false;
			uint32 NewImmLo = ((uint32)NewOff) & 3;
			uint32 NewImmHi = (((uint32)NewOff) >> 2) & 0x7FFFF;
			*Dst = (Insn & 0x9F00001F) | (NewImmLo << 29) | (NewImmHi << 5);
			return true;
		}

		// ADRP: imm21 * 4KB, page-relative
		if ((Insn & 0x9F000000) == 0x90000000)
		{
			int32 ImmLo = (Insn >> 29) & 3;
			int32 ImmHi = (int32)((Insn >> 5) & 0x7FFFF);
			if (ImmHi & 0x40000) ImmHi |= (int32)0xFFF80000;
			int32 Imm21 = (ImmHi << 2) | ImmLo;
			intptr_t SrcPage = reinterpret_cast<intptr_t>(Src) & ~0xFFFll;
			intptr_t DstPage = reinterpret_cast<intptr_t>(Dst) & ~0xFFFll;
			intptr_t AbsPage = SrcPage + ((intptr_t)Imm21 << 12);
			intptr_t NewOff = (AbsPage - DstPage) >> 12;
			if (NewOff < -(1 << 20) || NewOff >= (1 << 20))
				return false;
			uint32 NewImmLo = ((uint32)NewOff) & 3;
			uint32 NewImmHi = (((uint32)NewOff) >> 2) & 0x7FFFF;
			*Dst = (Insn & 0x9F00001F) | (NewImmLo << 29) | (NewImmHi << 5);
			return true;
		}

		// LDR literal: imm19 (bits 23:5), loads from PC + imm19*4
		if ((Insn & 0x3B000000) == 0x18000000)
		{
			int32 Imm19 = (int32)((Insn >> 5) & 0x7FFFF);
			if (Imm19 & 0x40000) Imm19 |= (int32)0xFFF80000;
			intptr_t AbsTarget = reinterpret_cast<intptr_t>(Src) + (intptr_t)Imm19 * 4;
			intptr_t NewOff = AbsTarget - reinterpret_cast<intptr_t>(Dst);
			int32 NewImm = (int32)(NewOff / 4);
			if (NewImm < -(1 << 18) || NewImm >= (1 << 18))
				return false;
			*Dst = (Insn & ~(0x7FFFF << 5)) | ((NewImm & 0x7FFFF) << 5);
			return true;
		}

		return true; // non-PC-relative instruction, copy as-is is fine
	}
#endif

} // anonymous namespace

// ==================== Public API ====================

void* Install(void* Target, void* Hook)
{
	if (!Target || !Hook)
		return nullptr;

	if (FindEntry(Target))
		return nullptr; // already hooked

#if PLATFORM_CPU_X86_FAMILY
	// x64: short JMP (5 bytes) at target → hook (via trampoline if far)
	static constexpr uint32 kJmpSize = 5; // E9 rel32

	const uint8* TargetBytes = reinterpret_cast<const uint8*>(Target);
	uint32 CopySize = FindInstructionBoundary(TargetBytes, kJmpSize);
	if (CopySize == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("GMPInlineHook: failed to decode instructions at %p"), Target);
		return nullptr;
	}

	// Allocate trampoline nearby
	// Trampoline layout: [relocated original bytes] [JMP abs64 back to target+CopySize]
	static constexpr uint32 kAbsJmpSize = 14; // FF 25 00 00 00 00 + 8-byte addr
	uint32 TrampolineSize = CopySize + kAbsJmpSize;
	uint8* Trampoline = AllocateNearby(Target, TrampolineSize);
	if (!Trampoline)
	{
		UE_LOG(LogTemp, Error, TEXT("GMPInlineHook: failed to allocate trampoline near %p"), Target);
		return nullptr;
	}

	// Copy + relocate original instructions to trampoline
	RelocateX64(Trampoline, TargetBytes, CopySize);

	// Append absolute JMP back to Target + CopySize
	uint8* BackJmp = Trampoline + CopySize;
	BackJmp[0] = 0xFF;
	BackJmp[1] = 0x25;
	*reinterpret_cast<uint32*>(&BackJmp[2]) = 0; // RIP-relative 0
	*reinterpret_cast<uint64*>(&BackJmp[6]) = reinterpret_cast<uint64>(TargetBytes + CopySize);

	// Save entry
	FHookEntry Entry;
	Entry.Target = Target;
	Entry.Trampoline = Trampoline;
	FMemory::Memcpy(Entry.SavedBytes, TargetBytes, CopySize);
	Entry.SavedSize = CopySize;
	GHookEntries.Add(Entry);

	// Write JMP at target → hook
	if (!SetMemoryRWX(Target, kJmpSize))
	{
		UE_LOG(LogTemp, Error, TEXT("GMPInlineHook: failed to change memory protection at %p"), Target);
		GHookEntries.Pop();
		return nullptr;
	}

	// Check if hook is within ±2GB (short JMP)
	intptr_t Delta = reinterpret_cast<intptr_t>(Hook) - (reinterpret_cast<intptr_t>(Target) + kJmpSize);
	if (FMath::Abs(Delta) < (intptr_t)kMaxDelta)
	{
		// Direct short JMP to hook
		uint8* P = reinterpret_cast<uint8*>(Target);
		P[0] = 0xE9;
		*reinterpret_cast<int32*>(&P[1]) = (int32)Delta;
	}
	else
	{
		// Need intermediate trampoline for far JMP
		uint8* FarTramp = AllocateNearby(Target, kAbsJmpSize);
		if (!FarTramp)
		{
			// Restore and fail
			FMemory::Memcpy(Target, Entry.SavedBytes, CopySize);
			GHookEntries.Pop();
			return nullptr;
		}
		// Far trampoline → hook
		FarTramp[0] = 0xFF;
		FarTramp[1] = 0x25;
		*reinterpret_cast<uint32*>(&FarTramp[2]) = 0;
		*reinterpret_cast<uint64*>(&FarTramp[6]) = reinterpret_cast<uint64>(Hook);

		// Target → short JMP to far trampoline
		uint8* P = reinterpret_cast<uint8*>(Target);
		P[0] = 0xE9;
		*reinterpret_cast<int32*>(&P[1]) = (int32)(reinterpret_cast<intptr_t>(FarTramp) - (reinterpret_cast<intptr_t>(Target) + kJmpSize));
	}

	FlushInstructionCache(Target, CopySize);
	FlushInstructionCache(Trampoline, TrampolineSize);

	return Trampoline;

#elif PLATFORM_CPU_ARM_FAMILY
	// arm64: 16-byte hook (LDR X16, #8; BR X16; .quad hook_addr)
	const uint32* TargetInsns = reinterpret_cast<const uint32*>(Target);
	uint32 CopySize = kARM64HookSize; // 4 instructions = 16 bytes

	// Allocate trampoline
	// Layout: [4 relocated instructions] [LDR X16, #8; BR X16; .quad (target+16)]
	uint32 TrampolineSize = CopySize + kARM64HookSize;
	uint8* Trampoline = AllocateNearby(Target, TrampolineSize);
	if (!Trampoline)
	{
		UE_LOG(LogTemp, Error, TEXT("GMPInlineHook: failed to allocate trampoline"));
		return nullptr;
	}

	// Relocate original 4 instructions
	uint32* TrampolineInsns = reinterpret_cast<uint32*>(Trampoline);
	for (uint32 i = 0; i < 4; ++i)
	{
		intptr_t Delta = reinterpret_cast<intptr_t>(&TrampolineInsns[i]) - reinterpret_cast<intptr_t>(&TargetInsns[i]);
		if (!RelocateARM64Instruction(&TrampolineInsns[i], &TargetInsns[i], Delta))
		{
			UE_LOG(LogTemp, Error, TEXT("GMPInlineHook: arm64 instruction relocation failed at %p+%d"), Target, i * 4);
			return nullptr;
		}
	}

	// Append absolute branch back to target+16
	WriteAbsoluteBranch(Trampoline + CopySize, reinterpret_cast<uintptr_t>(TargetInsns) + CopySize);

	// Save entry
	FHookEntry Entry;
	Entry.Target = Target;
	Entry.Trampoline = Trampoline;
	FMemory::Memcpy(Entry.SavedBytes, Target, CopySize);
	Entry.SavedSize = CopySize;
	GHookEntries.Add(Entry);

	// Write hook at target
	if (!SetMemoryRWX(Target, CopySize))
	{
		UE_LOG(LogTemp, Error, TEXT("GMPInlineHook: failed to change memory protection at %p"), Target);
		GHookEntries.Pop();
		return nullptr;
	}

	WriteAbsoluteBranch(reinterpret_cast<uint8*>(Target), reinterpret_cast<uintptr_t>(Hook));

	FlushInstructionCache(Target, CopySize);
	FlushInstructionCache(Trampoline, TrampolineSize);

	return Trampoline;

#else
#error "Unsupported CPU"
#endif
}

void Uninstall(void* Target)
{
	FHookEntry* Entry = FindEntry(Target);
	if (!Entry)
		return;

	if (SetMemoryRWX(Target, Entry->SavedSize))
	{
		FMemory::Memcpy(Target, Entry->SavedBytes, Entry->SavedSize);
		FlushInstructionCache(Target, Entry->SavedSize);
	}

	GHookEntries.RemoveAllSwap([Target](const FHookEntry& E) { return E.Target == Target; });
}

} // namespace GMPHook

#endif // WITH_EDITOR
