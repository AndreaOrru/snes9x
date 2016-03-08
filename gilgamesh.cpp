#ifdef DEBUGGER

#include <unordered_map>
#include <unordered_set>
#include <sqlite3.h>
#include "snes9x.h"
#include "display.h"
#include "debug.h"
#include "gilgamesh.h"

extern int AddrModes[256];

static sqlite3* Database;

struct SInstruction
{
	union
	{
		uint32 PC;
		struct
		{
			uint16 Address;
			uint8  Bank;
		};
	};

	uint8 Opcode;
	uint8 Size;
	
	int Operand = -1;
	std::unordered_set<int> References;
	std::unordered_set<int> IndirectReferences;

    SInstruction(uint32 PC) : PC(PC) {}
	
	void Decode()
	{
		int Reference = -1;
		int IndirectReference = -1;

		uint8 Operands[3];
	
		Opcode      = S9xDebugGetByte(PC);
		Operands[0] = S9xDebugGetByte(PC + 1);
		Operands[1] = S9xDebugGetByte(PC + 2);
		Operands[2] = S9xDebugGetByte(PC + 3);

		switch (AddrModes[Opcode])
		{
			// Implied:
			case 0:
				Size = 1;
				break;

			// Immediate[MemoryFlag]:
			case 1:
				if (!CheckFlag(MemoryFlag))
				{
					// Accumulator 16-bits:
					Operand = (Operands[1] << 8) | Operands[0];
					Size = 3;
				}
				else
				{
					// Accumulator 8-bits:
					Operand = Operands[0];
					Size = 2;
				}
				break;

			// Immediate[IndexFlag]:
			case 2:
				if (!CheckFlag(MemoryFlag))
				{
					// X/Y 16-bits:
					Operand = (Operands[1] << 8) | Operands[0];
					Size = 3;
				}
				else
				{
					// X/Y 8-bits:
					Operand = Operands[0];
					Size = 2;
				}
				break;

			// Immediate (always 8-bits):
			case 3:
				Operand = Operands[0];
				Size = 2;
				break;

			// Relative:
			case 4:
				Operand = Operands[0];
				Reference = Address + (int)Operand + 2;
				Size = 2;
				break;

			// Relative Long:
			case 5:
				Operand = (Operands[1] << 8) | Operands[0];
				Reference = Address + (int)Operand + 3;
				Size = 3;
				break;

			// Direct Page:
			case 6:
				Operand = Operands[0];
				Reference = Operand + Registers.D.W;
				Size = 2;
				break;

			// Direct Page Indexed (with X):
			case 7:
				Operand = Operands[0];
				Reference = Operand + Registers.D.W + Registers.X.W;
				Size = 2;
				break;

			// Direct Page Indexed (with Y):
			case 8:
				Operand = Operands[0];
				Reference = Operand + Registers.D.W + Registers.Y.W;
				Size = 2;
				break;

			// Direct Page Indirect:
			case 9:
				Operand = Operands[0];
				Reference = Operand + Registers.D.W;
				IndirectReference = (Registers.DB << 16) | S9xDebugGetWord(Reference);
				Size = 2;
				break;

			// Direct Page Indexed Indirect:
			case 10:
				Operand = Operands[0];
				Reference = Operand + Registers.D.W + Registers.X.W;
				IndirectReference = (Registers.DB << 16) | S9xDebugGetWord(Reference);
				Size = 2;
				break;

			// Direct Page Indirect Indexed:
			case 11:
				Operand = Operands[0];
				Reference = Operand + Registers.D.W;
				IndirectReference = (Registers.DB << 16) | ((S9xDebugGetWord(Reference) + Registers.Y.W) & 0xFFFF);
				Size = 2;
				break;

			// Direct Page Indirect Long:
			case 12:
				Operand = Operands[0];
				Reference = Operand + Registers.D.W;
				IndirectReference = (S9xDebugGetByte(Reference + 2) << 16) | S9xDebugGetWord(Reference);
				Size = 2;
				break;

			// Direct Page Indirect Indexed Long:
			case 13:
				Operand = Operands[0];
				Reference = Operand + Registers.D.W;
				IndirectReference = (S9xDebugGetByte(Reference + 2) << 16) | ((S9xDebugGetWord(Reference) + Registers.Y.W) & 0xFFFF);
				Size = 2;
				break;

			// Absolute:
			case 14:
				Operand = (Operands[1] << 8) | Operands[0];
				Reference = (Registers.DB << 16) | Operand;
				Size = 3;
				break;

			// Absolute Indexed (with X):
			case 15:
				Operand = (Operands[1] << 8) | Operands[0];
				Reference = (Registers.DB << 16) | (Operand + Registers.X.W);
				Size = 3;
				break;

			// Absolute Indexed (with Y):
			case 16:
				Operand = (Operands[1] << 8) | Operands[0];
				Reference = (Registers.DB << 16) | (Operand + Registers.Y.W);
				Size = 3;
				break;

			// Absolute Long:
			case 17:
				Operand = (Operands[2] << 16) | (Operands[1] << 8) | Operands[0];
				Reference = Operand;
				Size = 4;
				break;

			// Absolute Indexed Long:
			case 18:
				Operand = (Operands[2] << 16) | (Operands[1] << 8) | Operands[0];
				Reference = (Operands[2] << 16) | (((Operands[1] << 8) + Operands[0] + Registers.X.W) & 0xFFFF);
				Size = 4;
				break;

			// Stack Relative:
			case 19:
				Operand = Operands[0];
				/* Reference = Operand + Registers.S.W; */  // Don't keep track of stack positions.
				Size = 2;
				break;

			// Stack Relative Indirect Indexed:
			case 20:
				Operand = Operands[0];
				Reference = Operand + Registers.S.W;
				IndirectReference = (Registers.DB << 16) | ((S9xDebugGetWord(Reference) + Registers.Y.W) & 0xFFFF);
				Reference = -1;  // Don't keep track of stack positions.
				Size = 2;
				break;

			// Absolute Indirect:
			case 21:
				Operand = (Operands[1] << 8) | Operands[0];
				Reference = Operand;
				IndirectReference = (Registers.PB << 16) | S9xDebugGetWord(Reference);
				Size = 3;
				break;

			// Absolute Indirect Long:
			case 22:
				Operand = (Operands[1] << 8) | Operands[0];
				Reference = Operand;
				IndirectReference = (S9xDebugGetByte(Reference + 2) << 16) | S9xDebugGetWord(Reference);
				Size = 3;
				break;

			// Absolute Indexed Indirect:
			case 23:
				Operand = (Operands[1] << 8) | Operands[0];
				Reference = (Registers.PB << 16) | ((Operand + Registers.X.W) & 0xFFFF);
				IndirectReference = S9xDebugGetWord(Reference);
				Size = 3;
				break;

			// Implied Accumulator:
			case 24:
				Size = 1;
				break;

			// MVN/MVP src, dst:
			case 25:
				Operand = (Operands[1] << 8) | Operands[0];
				// TODO: Multiple references.
				Size = 3;
				break;

			// PEA:
			case 26:
				Operand = (Operands[1] << 8) | Operands[0];
				// TODO: Check if it makes sense to consider it a reference.
				Size = 3;
				break;

			// PEI Direct Page Indirect:
			case 27:
				Operand = Operands[0];
				Reference = Operand + Registers.D.W;
				// IndirectReference = S9xDebugGetWord(Reference);
				// TODO: Should it be counted as a reference?
				Size = 2;
				break;
		}

		if (Reference != -1)
			this->References.insert(Reference);

		if (IndirectReference != -1)
			this->IndirectReferences.insert(IndirectReference);
	}
};

static std::unordered_map<uint32, SInstruction> Instructions;

template<typename... TArgs> void SQL(const char* Format, TArgs... Args)
{
    static char S[4096];
    sprintf(S, Format, Args...);
    sqlite3_exec(Database, S, NULL, NULL, NULL);
}

void GilgameshTrace(uint8 Bank, uint16 Address)
{
	uint32 PC = (Bank << 16) | Address;

	/* Search instruction by PC.
	 * - If it's already present, fetch it.
	 * - Otherwise, create an "empty" instruction with the given PC.
	 * Then decode the instruction. */
	Instructions.emplace(std::piecewise_construct,
	                     std::forward_as_tuple(PC),
	                     std::forward_as_tuple(PC)).first->second.Decode();
}

void GilgameshSave()
{
    std::string DatabasePath = S9xGetDirectory(LOG_DIR);
    DatabasePath += "/gilgamesh.db";
    sqlite3_open(DatabasePath.c_str(), &Database);

    SQL("CREATE TABLE instructions(pc      INTEGER PRIMARY KEY,"
                                  "opcode  INTEGER NOT NULL,"
                                  "size    INTEGER NOT NULL,"
                                  "operand INTEGER)");

    SQL("CREATE TABLE direct_references(pointer INTEGER,"
                                       "pointee INTEGER,"
                                       "PRIMARY KEY (pointer, pointee))");

    SQL("CREATE TABLE indirect_references(pointer INTEGER,"
                                         "pointee INTEGER,"
                                         "PRIMARY KEY (pointer, pointee))");
    
    SQL("BEGIN TRANSACTION");    
    for (auto& KeyValue: Instructions)
	{
		SInstruction& I = KeyValue.second;

        if (I.Operand != -1)
            SQL("INSERT INTO instructions VALUES(%u, %u, %u, %d)",   I.PC, I.Opcode, I.Size, I.Operand);
        else
            SQL("INSERT INTO instructions VALUES(%u, %u, %u, NULL)", I.PC, I.Opcode, I.Size);

        for (int DirectReference: I.References)
            SQL("INSERT INTO direct_references VALUES(%d, %d)",   I.PC, DirectReference);

        for (int IndirectReference: I.IndirectReferences)
            SQL("INSERT INTO indirect_references VALUES(%d, %d)", I.PC, IndirectReference);
	}
    SQL("COMMIT TRANSACTION");

    sqlite3_close(Database);
}

#endif
