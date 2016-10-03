#ifdef DEBUGGER

#ifndef _GILGAMESH_H_
#define _GILGAMESH_H_

enum VectorType
{
    VECTOR_RESET = 0,
    VECTOR_NMI = 1,
    VECTOR_IRQ = 2
};

void GilgameshSave();
void GilgameshTrace(uint8 Bank, uint16 Address);
void GilgameshTraceVector(uint32 PC, VectorType Type);

#endif

#endif
