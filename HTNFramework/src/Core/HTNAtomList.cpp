// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Core/HTNAtomList.h"
#include "Core/HTNAtom.h"
#include "Core/HTNAtomListAllocator.h"
#include "Core/HTNAtomNode.h"

namespace
{
HTNAtomListAllocator* GetAllocator(const HTNAtomList* inList)
{
    return inList ? static_cast<HTNAtomListAllocator*>(inList->allocator) : nullptr;
}

void AppendNode(HTNAtomList& ioList, HTNAtomNode& inNode)
{
    inNode.next_node = nullptr;
    if (!ioList.head_node)
        ioList.head_node = ioList.tail_node = &inNode;
    else
    {
        ioList.tail_node->next_node = &inNode;
        ioList.tail_node = &inNode;
    }
    ++ioList.size;
}
}

extern "C" void HTNAtomList_Init(HTNAtomList* ioList)
{
    if (!ioList)
        return;
    ioList->allocator = &HTNNewDeleteAtomListAllocator::Get();
    ioList->head_node = nullptr;
    ioList->tail_node = nullptr;
    ioList->size = 0u;
}

extern "C" void HTNAtomList_InitWithAllocator(HTNAtomList* ioList, void* inAllocator)
{
    if (!ioList)
        return;
    ioList->allocator = inAllocator ? inAllocator : static_cast<void*>(&HTNNewDeleteAtomListAllocator::Get());
    ioList->head_node = nullptr;
    ioList->tail_node = nullptr;
    ioList->size = 0u;
}

extern "C" void HTNAtomList_Destroy(HTNAtomList* ioList)
{
    if (!ioList)
        return;
    HTNAtomList_Clear(ioList);
}

extern "C" int HTNAtomList_Copy(HTNAtomList* outList, const HTNAtomList* inList)
{
    if (!outList || !inList)
        return 0;
    if (outList == inList)
        return 1;

    HTNAtomList_Clear(outList);
    /* Copy values into the allocator already selected by the destination.
       Copy constructors that want to preserve the source allocator initialize
       the destination with it before calling this function. Assignments and
       explicit client-owned destinations keep their own allocator. */

    for (const HTNAtomNode* Current = inList->head_node; Current; Current = Current->next_node)
    {
        if (!HTNAtomList_PushBack(outList, &Current->data))
        {
            HTNAtomList_Clear(outList);
            return 0;
        }
    }
    return 1;
}

extern "C" void HTNAtomList_Move(HTNAtomList* outList, HTNAtomList* inList)
{
    if (!outList || !inList || outList == inList)
        return;

    HTNAtomList_Clear(outList);
    /* Move steals the nodes themselves, so their allocator is part of the transferred
       ownership. Keeping the destination allocator would later deallocate these nodes
       through an allocator that did not create them. */
    outList->allocator = inList->allocator;
    outList->head_node = inList->head_node;
    outList->tail_node = inList->tail_node;
    outList->size = inList->size;

    inList->head_node = nullptr;
    inList->tail_node = nullptr;
    inList->size = 0u;
}

extern "C" int HTNAtomList_Equals(const HTNAtomList* inLeft, const HTNAtomList* inRight)
{
    if (!inLeft || !inRight)
        return inLeft == inRight;

    const HTNAtomNode* Left = inLeft->head_node;
    const HTNAtomNode* Right = inRight->head_node;
    while (Left && Right)
    {
        if (!HTNAtom_Equals(&Left->data, &Right->data))
            return 0;
        Left = Left->next_node;
        Right = Right->next_node;
    }
    return !Left && !Right;
}

extern "C" int HTNAtomList_PushBack(HTNAtomList* ioList, const HTNAtom* inValue)
{
    if (!ioList || !inValue)
        return 0;

    HTNAtomListAllocator* Allocator = GetAllocator(ioList);
    if (!Allocator)
        return 0;

    HTNAtomNode* Node = Allocator->Allocate();
    if (!Node)
        return 0;
    if (!HTNAtom_Copy(&Node->data, inValue))
    {
        Allocator->Deallocate(Node);
        return 0;
    }

    AppendNode(*ioList, *Node);
    return 1;
}

extern "C" int HTNAtomList_PushBackMove(HTNAtomList* ioList, HTNAtom* ioValue)
{
    if (!ioList || !ioValue)
        return 0;

    HTNAtomListAllocator* Allocator = GetAllocator(ioList);
    if (!Allocator)
        return 0;

    HTNAtomNode* Node = Allocator->Allocate();
    if (!Node)
        return 0;

    HTNAtom_Move(&Node->data, ioValue);
    AppendNode(*ioList, *Node);
    return 1;
}

extern "C" int HTNAtomList_PopFrontMove(HTNAtomList* ioList, HTNAtom* outValue)
{
    if (!ioList || !outValue || !ioList->head_node)
        return 0;

    HTNAtomListAllocator* Allocator = GetAllocator(ioList);
    if (!Allocator)
        return 0;

    HTNAtomNode* Node = ioList->head_node;
    ioList->head_node = Node->next_node;
    if (!ioList->head_node)
        ioList->tail_node = nullptr;
    --ioList->size;

    HTNAtom_Move(outValue, &Node->data);
    Allocator->Deallocate(Node);
    return 1;
}

extern "C" int HTNAtomList_RemoveAt(HTNAtomList* ioList, const uint32_t inIndex)
{
    if (!ioList || inIndex >= ioList->size)
        return 0;

    HTNAtomNode* Previous = nullptr;
    HTNAtomNode* Current = ioList->head_node;
    for (uint32_t Index = 0; Index < inIndex; ++Index)
    {
        Previous = Current;
        Current = Current->next_node;
    }

    HTNAtomListAllocator* Allocator = GetAllocator(ioList);
    if (!Allocator)
        return 0;

    HTNAtomNode* Next = Current->next_node;
    if (Previous)
        Previous->next_node = Next;
    else
        ioList->head_node = Next;
    if (ioList->tail_node == Current)
        ioList->tail_node = Previous;

    Allocator->Deallocate(Current);
    --ioList->size;
    return 1;
}

extern "C" void HTNAtomList_Clear(HTNAtomList* ioList)
{
    if (!ioList)
        return;

    HTNAtomListAllocator* Allocator = GetAllocator(ioList);
    for (HTNAtomNode* Current = ioList->head_node; Current;)
    {
        HTNAtomNode* Next = Current->next_node;
        if (Allocator)
            Allocator->Deallocate(Current);
        Current = Next;
    }
    ioList->head_node = nullptr;
    ioList->tail_node = nullptr;
    ioList->size = 0u;
}

extern "C" const HTNAtom* HTNAtomList_Get(const HTNAtomList* inList, const uint32_t inIndex)
{
    if (!inList || inIndex >= inList->size)
        return nullptr;
    const HTNAtomNode* Current = inList->head_node;
    for (uint32_t Index = 0; Index < inIndex; ++Index)
        Current = Current->next_node;
    return &Current->data;
}

extern "C" uint32_t HTNAtomList_GetSize(const HTNAtomList* inList)
{
    return inList ? inList->size : 0u;
}

extern "C" int HTNAtomList_IsEmpty(const HTNAtomList* inList)
{
    return !inList || inList->size == 0u;
}

extern "C" int HTNAtomList_Split(const HTNAtomList* inList, const HTNAtomListSplitDirection inDirection,
                                   HTNAtom* outElement, HTNAtom* outRemainder)
{
    if (!inList || !outElement || !outRemainder || outElement == outRemainder || inList->size == 0u)
        return 0;
    if (inDirection != HTN_ATOM_LIST_SPLIT_FRONT && inDirection != HTN_ATOM_LIST_SPLIT_BACK)
        return 0;

    const uint32_t ElementIndex = inDirection == HTN_ATOM_LIST_SPLIT_FRONT ? 0u : inList->size - 1u;
    const HTNAtom* Element = HTNAtomList_Get(inList, ElementIndex);
    if (!Element)
        return 0;

    HTNAtom ElementCopy;
    HTNAtom Remainder;
    HTNAtom_Init(&Remainder);
    HTNAtomList_InitWithAllocator(&Remainder.value.list_value, inList->allocator);
    Remainder.type = HTN_ATOM_TYPE_LIST;

    if (!HTNAtom_Copy(&ElementCopy, Element))
    {
        HTNAtom_Destroy(&ElementCopy);
        HTNAtom_Destroy(&Remainder);
        return 0;
    }

    for (uint32_t Index = 0u; Index < inList->size; ++Index)
    {
        if (Index == ElementIndex)
            continue;
        const HTNAtom* Value = HTNAtomList_Get(inList, Index);
        if (!Value || !HTNAtom_PushBackListElement(&Remainder, Value))
        {
            HTNAtom_Destroy(&ElementCopy);
            HTNAtom_Destroy(&Remainder);
            return 0;
        }
    }

    HTNAtom_AssignMove(outElement, &ElementCopy);
    HTNAtom_AssignMove(outRemainder, &Remainder);
    HTNAtom_Destroy(&ElementCopy);
    HTNAtom_Destroy(&Remainder);
    return 1;
}

std::string HTNAtomListToString(const HTNAtomList& inList, const bool inShouldDoubleQuoteString)
{
    std::string Result = "(";
    bool First = true;
    for (const HTNAtomNode* Current = inList.head_node; Current; Current = Current->next_node)
    {
        if (!First)
            Result.push_back(' ');
        Result.append(HTNAtomToString(Current->data, inShouldDoubleQuoteString));
        First = false;
    }
    Result.push_back(')');
    return Result;
}
