#ifdef DEBUGGER

#include <unordered_map>
#include <unordered_set>
#include <sqlite3.h>
#include "snes9x.h"
#include "display.h"
#include "debug.h"
#include "gilgamesh.h"

#define SQL(...) \
    do { if (SQLExec(__VA_ARGS__) != SQLITE_OK) return; } while (0)

static const int UNDEFINED = -1;
enum { DIRECT_REFERENCE = 0, INDIRECT_REFERENCE = 1 };

static sqlite3* Database;
extern int AddrModes[256];

template<typename... TArgs> int SQLExec(const char* Format, TArgs... Args)
{
    int Status;
    char* Error;

    static char S[4096];
    sprintf(S, Format, Args...);

    Status = sqlite3_exec(Database, S, NULL, NULL, &Error);
    if (Status != SQLITE_OK)
    {
        fprintf(stderr, "SQL error: %s\n", Error);
        sqlite3_free(Error);
        sqlite3_close(Database);
    }

    return Status;
}

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
    uint8 Flags;

    int Operand = UNDEFINED;
    std::unordered_set<int> References;
    std::unordered_set<int> IndirectReferences;

    explicit SInstruction(uint32 PC) : PC(PC) {}

    void Decode()
    {
        int Reference = UNDEFINED;
        int IndirectReference = UNDEFINED;
        uint8 Operands[3];

        Opcode      = S9xDebugGetByte(PC);
        Operands[0] = S9xDebugGetByte(PC + 1);
        Operands[1] = S9xDebugGetByte(PC + 2);
        Operands[2] = S9xDebugGetByte(PC + 3);

        Flags = Registers.P.B.l;

        switch (AddrModes[Opcode])
        {
            // Implied:
            case 0:
                break;

            // Immediate[MemoryFlag]:
            case 1:
                if (!CheckFlag(MemoryFlag))
                {
                    // Accumulator 16-bits:
                    Operand = (Operands[1] << 8) | Operands[0];
                }
                else
                {
                    // Accumulator 8-bits:
                    Operand = Operands[0];
                }
                break;

            // Immediate[IndexFlag]:
            case 2:
                if (!CheckFlag(IndexFlag))
                {
                    // X/Y 16-bits:
                    Operand = (Operands[1] << 8) | Operands[0];
                }
                else
                {
                    // X/Y 8-bits:
                    Operand = Operands[0];
                }
                break;

            // Immediate (always 8-bits):
            case 3:
                Operand = Operands[0];
                break;

            // Relative:
            case 4:
                Operand = Operands[0];
                Reference = Address + (int8)Operand + 2;
                break;

            // Relative Long:
            case 5:
                Operand = (Operands[1] << 8) | Operands[0];
                Reference = Address + (int16)Operand + 3;
                break;

            // Direct Page:
            case 6:
                Operand = Operands[0];
                Reference = Operand + Registers.D.W;
                break;

            // Direct Page Indexed (with X):
            case 7:
                Operand = Operands[0];
                Reference = Operand + Registers.D.W + Registers.X.W;
                break;

            // Direct Page Indexed (with Y):
            case 8:
                Operand = Operands[0];
                Reference = Operand + Registers.D.W + Registers.Y.W;
                break;

            // Direct Page Indirect:
            case 9:
                Operand = Operands[0];
                Reference = Operand + Registers.D.W;
                IndirectReference = (Registers.DB << 16) | S9xDebugGetWord(Reference);
                break;

            // Direct Page Indexed Indirect:
            case 10:
                Operand = Operands[0];
                Reference = Operand + Registers.D.W + Registers.X.W;
                IndirectReference = (Registers.DB << 16) | S9xDebugGetWord(Reference);
                break;

            // Direct Page Indirect Indexed:
            case 11:
                Operand = Operands[0];
                Reference = Operand + Registers.D.W;
                IndirectReference = (Registers.DB << 16) | ((S9xDebugGetWord(Reference) + Registers.Y.W) & 0xFFFF);
                break;

            // Direct Page Indirect Long:
            case 12:
                Operand = Operands[0];
                Reference = Operand + Registers.D.W;
                IndirectReference = (S9xDebugGetByte(Reference + 2) << 16) | S9xDebugGetWord(Reference);
                break;

            // Direct Page Indirect Indexed Long:
            case 13:
                Operand = Operands[0];
                Reference = Operand + Registers.D.W;
                IndirectReference = (S9xDebugGetByte(Reference + 2) << 16) | ((S9xDebugGetWord(Reference) + Registers.Y.W) & 0xFFFF);
                break;

            // Absolute:
            case 14:
                Operand = (Operands[1] << 8) | Operands[0];
                Reference = (Registers.DB << 16) | Operand;
                break;

            // Absolute Indexed (with X):
            case 15:
                Operand = (Operands[1] << 8) | Operands[0];
                Reference = (Registers.DB << 16) | (Operand + Registers.X.W);
                break;

            // Absolute Indexed (with Y):
            case 16:
                Operand = (Operands[1] << 8) | Operands[0];
                Reference = (Registers.DB << 16) | (Operand + Registers.Y.W);
                break;

            // Absolute Long:
            case 17:
                Operand = (Operands[2] << 16) | (Operands[1] << 8) | Operands[0];
                Reference = Operand;
                break;

            // Absolute Indexed Long:
            case 18:
                Operand = (Operands[2] << 16) | (Operands[1] << 8) | Operands[0];
                Reference = (Operands[2] << 16) | (((Operands[1] << 8) + Operands[0] + Registers.X.W) & 0xFFFF);
                break;

            // Stack Relative:
            case 19:
                Operand = Operands[0];
                /* Reference = Operand + Registers.S.W; */  // Don't keep track of stack positions.
                break;

            // Stack Relative Indirect Indexed:
            case 20:
                Operand = Operands[0];
                Reference = Operand + Registers.S.W;
                IndirectReference = (Registers.DB << 16) | ((S9xDebugGetWord(Reference) + Registers.Y.W) & 0xFFFF);
                Reference = UNDEFINED;  // Don't keep track of stack positions.
                break;

            // Absolute Indirect:
            case 21:
                Operand = (Operands[1] << 8) | Operands[0];
                Reference = Operand;
                IndirectReference = (Registers.PB << 16) | S9xDebugGetWord(Reference);
                break;

            // Absolute Indirect Long:
            case 22:
                Operand = (Operands[1] << 8) | Operands[0];
                Reference = Operand;
                IndirectReference = (S9xDebugGetByte(Reference + 2) << 16) | S9xDebugGetWord(Reference);
                break;

            // Absolute Indexed Indirect:
            case 23:
                Operand = (Operands[1] << 8) | Operands[0];
                Reference = (Registers.PB << 16) | ((Operand + Registers.X.W) & 0xFFFF);
                IndirectReference = S9xDebugGetWord(Reference);
                break;

            // Implied Accumulator:
            case 24:
                break;

            // MVN/MVP src, dst:
            case 25:
                Operand = (Operands[1] << 8) | Operands[0];
                // TODO: Multiple references.
                break;

            // PEA:
            case 26:
                Operand = (Operands[1] << 8) | Operands[0];
                // TODO: Check if it makes sense to consider it a reference.
                break;

            // PEI Direct Page Indirect:
            case 27:
                Operand = Operands[0];
                Reference = Operand + Registers.D.W;
                // IndirectReference = S9xDebugGetWord(Reference);
                // TODO: Should it be counted as a reference?
                break;
        }

        if (Reference != UNDEFINED)
            this->References.insert(Reference);

        if (IndirectReference != UNDEFINED)
            this->IndirectReferences.insert(IndirectReference);
    }
};

union SDMATransfer
{
    struct
    {
        unsigned source : 24;
        unsigned destination : 24;
        unsigned bytes : 16;
    };
    uint64 hash;

    bool operator==(const SDMATransfer& other) const { return hash == other.hash; }
};

template<> struct std::hash<SDMATransfer>
{
    size_t operator()(const SDMATransfer& DMATransfer) const { return DMATransfer.hash; }
};

static std::unordered_map<uint32, SInstruction> Instructions;
static std::unordered_map<uint32, VectorType> Vectors;
static std::unordered_set<SDMATransfer> DMATransfers;

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

void GilgameshTraceVector(uint32 PC, VectorType Type)
{
    Vectors[PC] = Type;
}

void GilgameshTraceDMA(SDMA& DMA)
{
    SDMATransfer DMATransfer = {
        .source      = (unsigned) ((DMA.ABank << 16) | DMA.AAddress),
        .destination = (unsigned) 0,
        .bytes       = (unsigned) DMA.TransferBytes,
    };

    DMATransfers.insert(DMATransfer);
}

void GilgameshSave()
{
    std::string DatabasePath = S9xGetDirectory(LOG_DIR);
    DatabasePath += "/gilgamesh.db";

    int Status = sqlite3_open(DatabasePath.c_str(), &Database);
    if (Status != SQLITE_OK)
    {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(Database));
        return;
    }

    SQL("DROP TABLE IF EXISTS instructions");
    SQL("CREATE TABLE instructions(pc      INTEGER PRIMARY KEY,"
                                  "opcode  INTEGER NOT NULL,"
                                  "flags   INTEGER NOT NULL,"
                                  "operand INTEGER)");

    SQL("DROP TABLE IF EXISTS references_");
    SQL("CREATE TABLE references_(pointer INTEGER,"
                                 "pointee INTEGER,"
                                 "type    INTEGER,"
                                 "PRIMARY KEY (pointer, pointee, type))");

    SQL("DROP TABLE IF EXISTS dma");
    SQL("CREATE TABLE dma(source      INTEGER,"
                         "destination INTEGER,"
                         "bytes       INTEGER,"
                         "PRIMARY KEY (source, destination, bytes))");

    SQL("DROP TABLE IF EXISTS vectors");
    SQL("CREATE TABLE vectors(pc   INTEGER PRIMARY KEY,"
                             "type INTEGER NOT NULL)");

    SQL("BEGIN TRANSACTION");
    for (auto& KeyValue: Instructions)
    {
        SInstruction& I = KeyValue.second;

        if (I.Operand != UNDEFINED)
            SQL("INSERT INTO instructions VALUES(%u, %u, %u, %d)", I.PC, I.Opcode, I.Flags, I.Operand);
        else
            SQL("INSERT INTO instructions VALUES(%u, %u, %u, NULL)", I.PC, I.Opcode, I.Flags);

        for (int DirectReference: I.References)
            SQL("INSERT INTO references_ VALUES(%d, %d, %d)", I.PC, DirectReference, DIRECT_REFERENCE);
        for (int IndirectReference: I.IndirectReferences)
            SQL("INSERT INTO references_ VALUES(%d, %d, %d)", I.PC, IndirectReference, INDIRECT_REFERENCE);
    }
    for (auto& DMATransfer: DMATransfers)
    {
        SQL("INSERT INTO dma VALUES(%d, %d, %d)",
            DMATransfer.source, DMATransfer.destination, DMATransfer.bytes);
    }
    for (auto& KeyValue: Vectors)
    {
        uint32 PC = KeyValue.first;
        VectorType Type = KeyValue.second;
        SQL("INSERT INTO vectors VALUES(%d, %d)", PC, Type);
    }
    SQL("COMMIT TRANSACTION");

    sqlite3_close(Database);
}

#endif
