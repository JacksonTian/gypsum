// Copyright 2014 Jay Conrod. All rights reserved.

// This file is part of CodeSwitch. Use of this source code is governed by
// the 3-clause BSD license that can be found in the LICENSE.txt file.


#include "test.h"

#include <cstring>
#include "array.h"
#include "block.h"
#include "block-visitor.h"
#include "bytecode.h"
#include "function.h"
#include "package.h"
#include "roots.h"
#include "type.h"

using namespace codeswitch::internal;
using namespace std;

class IncrementVisitor: public BlockVisitorBase<IncrementVisitor> {
 public:
  static const int kIgnoreMetaFlag = 1;

  IncrementVisitor(word_t flags = 0)
      : flags_(flags) { }

  void visitPointer(Block** p) {
    auto wp = reinterpret_cast<word_t*>(p);
    (*wp) += 4;
  }

  void visitMetaWord(Block* mw) {
    if ((flags_ & kIgnoreMetaFlag) != 0)
      return;
    else
      BlockVisitorBase<IncrementVisitor>::visitMetaWord(mw);
  }

 private:
  word_t flags_;
};


TEST(BlockVisitorEncodedMeta) {
  // This test requires us to initialize the heap, which requires a VM.
  VM vm(0);

  // The meta meta should have an encoded meta pointing to itself.
  Meta* metaMeta = vm.roots()->metaMeta();
  ASSERT_TRUE(metaMeta->metaWord().isBlockType());
  ASSERT_EQ(metaMeta->meta(), metaMeta);

  // The increment visitor should not visit encoded metas, since they aren't really pointers.
  IncrementVisitor visitor;
  visitor.visit(metaMeta);
  ASSERT_TRUE(metaMeta->metaWord().isBlockType());
  ASSERT_EQ(metaMeta->meta(), metaMeta);
}


TEST(BlockVisitorRegularMeta) {
  // This time, we'll create our own meta.
  VM vm;
  auto heap = vm.heap();
  AllowAllocationScope allowAllocation(heap, true);
  auto meta = new(heap, 0, kWordSize, 0) Meta(META_BLOCK_TYPE);

  // Our fake object will use this meta.
  word_t fake[] = { reinterpret_cast<word_t>(meta) };

  // The increment visitor should mutate the meta. Note that we avoid changing the low bits
  // because they are still used for GC.
  IncrementVisitor visitor;
  visitor.visit(reinterpret_cast<Block*>(fake));
  ASSERT_EQ(reinterpret_cast<word_t>(meta) + 4, fake[0]);
}


TEST(BlockVisitorRegularPointers) {
  VM vm;
  auto heap = vm.heap();
  AllowAllocationScope allowAllocation(heap, true);
  auto meta = new(heap, 0, 6 * kWordSize, 3 * kWordSize) Meta(OBJECT_BLOCK_TYPE);
  meta->hasPointers_ = true;
  meta->hasElementPointers_ = true;
  meta->lengthOffset_ = kWordSize;
  meta->objectPointerMap().setWord(0, 0x34);
  meta->elementPointerMap().setWord(0, 0x5);

  word_t fake[] = { reinterpret_cast<word_t>(meta), 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  IncrementVisitor visitor(IncrementVisitor::kIgnoreMetaFlag);
  visitor.visit(reinterpret_cast<Block*>(fake));
  word_t expected[] = { reinterpret_cast<word_t>(meta), 2, 4, 0, 4, 4, 4, 0, 4, 4, 0, 4 };
  for (word_t i = 0; i < ARRAY_LENGTH(fake); i++)
    ASSERT_EQ(expected[i], fake[i]);
}


static u8 makeSmallVbn(i64 value) {
  ASSERT(-128 <= value && value < 127);
  u8 vbn = static_cast<u8>(value & 0x7f);
  return vbn;
}


static Local<Package> createTestPackage(Heap* heap) {
  auto roots = heap->vm()->roots();
  auto typeList = BlockArray<Type>::create(heap, 3);

  typeList->set(0, Type::rootClassType(roots));
  typeList->set(1, Type::i8Type(roots));
  typeList->set(2, Type::rootClassType(roots));

  const u8 kByteIndex = makeSmallVbn(-1);
  const u8 kPtrIndex = makeSmallVbn(-2);
  const u8 kRootClassIndex = makeSmallVbn(-1);
  vector<u8> instList = {
    I8,
    0,
    STLOCAL,
    kByteIndex,
    ALLOCOBJ,
    kRootClassIndex,
    STLOCAL,
    kPtrIndex,
    LDLOCAL,
    kByteIndex,
    LDLOCAL,
    kPtrIndex,
    CALLG,
    2,
    0,
    LDLOCAL,
    kByteIndex,
    LDLOCAL,
    kPtrIndex,
    CALLG,
    2,
    0,
    RET,
  };
  auto blockOffsetList = LengthArray::create(heap, 1);
  blockOffsetList->set(0, 0);
  auto package = Package::create(heap);
  auto functions = BlockArray<Function>::create(heap, 1);
  Local<TaggedArray<TypeParameter>> emptyTypeParameters(
      reinterpret_cast<TaggedArray<TypeParameter>*>(roots->emptyTaggedArray()));
  auto function = Function::create(heap, 0, emptyTypeParameters, typeList,
                                   2 * kWordSize, instList, blockOffsetList, package);
  functions->set(0, *function);
  package->setFunctions(*functions);
  package->setEntryFunctionIndex(0);

  return package;
}


struct ExpectedPointerMap {
  word_t pcOffset;
  word_t count;
  word_t bits;
};


const ExpectedPointerMap kExpectedPointerMaps[] = {
  { 6, 2, 0x0 },
  { 15, 4, 0xa },
  { 22, 5, 0x16 },
};


TEST(BlockVisitorFunction) {
  VM vm(0);
  Heap* heap = vm.heap();
  AllowAllocationScope allowAllocation(heap, true);
  HandleScope handleScope(&vm);
  auto package = *createTestPackage(heap);
  auto function = package->getFunction(0);
  auto typeParameters = function->typeParameters();
  auto types = function->types();
  auto localsSize = function->localsSize();
  auto instructionsSize = function->instructionsSize();
  auto blockOffsets = function->blockOffsets();
  IncrementVisitor visitor;
  visitor.visit(function);
  word_t expected[] = {
      FUNCTION_BLOCK_TYPE << 2,
      0,
      0,
      reinterpret_cast<word_t>(typeParameters) + 4,
      reinterpret_cast<word_t>(types) + 4,
      localsSize,
      instructionsSize,
      reinterpret_cast<word_t>(blockOffsets) + 4,
      reinterpret_cast<word_t>(package) + 4,
      4,
  };
  for (word_t i = 0; i < ARRAY_LENGTH(expected); i++)
    ASSERT_EQ(expected[i], reinterpret_cast<word_t*>(function)[i]);
}


TEST(BuildStackPointerMap) {
  VM vm(0);
  auto heap = vm.heap();
  AllowAllocationScope allowAllocation(heap, true);
  HandleScope handleScope(&vm);
  auto package = createTestPackage(heap);
  auto function = handle(package->getFunction(0));
  auto pointerMap = StackPointerMap::buildFrom(heap, function);
  auto bitmap = pointerMap->bitmap();

  word_t paramExpected = 0x2;
  word_t paramOffset, paramCount;
  pointerMap->getParametersRegion(&paramOffset, &paramCount);
  ASSERT_EQ(2, paramCount);
  for (word_t i = 0; i < paramCount; i++) {
    bool expected = (paramExpected >> i & 1) == 1;
    ASSERT_EQ(expected, bitmap[paramOffset + i]);
  }

  for (word_t i = 0; i < ARRAY_LENGTH(kExpectedPointerMaps); i++) {
    word_t localsOffset, localsCount;
    pointerMap->getLocalsRegion(kExpectedPointerMaps[i].pcOffset, &localsOffset, &localsCount);
    ASSERT_EQ(kExpectedPointerMaps[i].count, localsCount);
    for (word_t j = 0; j < kExpectedPointerMaps[i].count; j++) {
      bool expected = (kExpectedPointerMaps[i].bits >> j & 1) == 1;
      ASSERT_EQ(expected, bitmap[localsOffset + j]);
    }
  }
}


class StackIncrementVisitor: public BlockVisitorBase<StackIncrementVisitor> {
 public:
  explicit StackIncrementVisitor(Function* function)
      : function_(function) { }

  void visitPointer(Block** p) {
    if (*p < reinterpret_cast<Block*>(1000))
      *reinterpret_cast<word_t*>(p) += 4;
    else
      ASSERT_EQ(function_, *p);
  }

 private:
  Block* function_;
};


TEST(VisitAndRelocateStack) {
  VM vm(0);
  auto heap = vm.heap();
  AllowAllocationScope allowAllocation(heap, true);
  HandleScope handleScope(&vm);
  auto stack = vm.stack();
  auto package = createTestPackage(heap);
  auto function = handle(package->getFunction(0));
  auto stackPointerMap = StackPointerMap::buildFrom(heap, function);
  function->setStackPointerMap(*stackPointerMap);

  // Construct some fake stack frames.
  const word_t kDataMarker = 0;
  const word_t kObjectMarker = 10;
  word_t startSp = stack->sp();
  // args
  stack->push<word_t>(kDataMarker);
  stack->push<word_t>(kObjectMarker);
  // frame
  stack->push<word_t>(kNotSet);
  stack->push<Function*>(*function);
  stack->push<Address>(stack->fp());
  stack->setFp(stack->sp());
  // locals
  stack->push<word_t>(kDataMarker);
  stack->push<word_t>(kObjectMarker);
  // expressions
  stack->push<word_t>(kObjectMarker);

  // args
  stack->push<word_t>(kDataMarker);
  stack->push<word_t>(kObjectMarker);
  // frame
  stack->push<word_t>(kExpectedPointerMaps[2].pcOffset);
  stack->push<Function*>(*function);
  stack->push<Address>(stack->fp());
  stack->setFp(stack->sp());
  // locals
  stack->push<word_t>(kDataMarker);
  stack->push<word_t>(kObjectMarker);

  // args
  stack->push<word_t>(kDataMarker);
  stack->push<word_t>(kObjectMarker);
  // frame
  stack->push<word_t>(kExpectedPointerMaps[1].pcOffset);
  stack->push<Function*>(*function);
  stack->push<Address>(stack->fp());
  stack->setFp(stack->sp());
  // locals
  stack->push<word_t>(kDataMarker);
  stack->push<word_t>(kDataMarker);
  stack->push<word_t>(kExpectedPointerMaps[0].pcOffset);

  auto endSp = stack->sp();

  // Copy the stack to a new location and relocate it, as the GC would do.
  ASSERT_TRUE(stack->meta()->needsRelocation());
  auto rawNewStack = heap->allocateUninitialized(stack->sizeOfBlock());
  copy_n(reinterpret_cast<u8*>(*stack),
         stack->sizeOfBlock(),
         reinterpret_cast<u8*>(rawNewStack));
  Local<Stack> newStack(&vm, reinterpret_cast<Stack*>(rawNewStack));
  auto delta = rawNewStack - stack->address();
  newStack->relocate(delta);

  // Increment pointers on the copied stack.
  StackIncrementVisitor visitor(*function);
  visitor.visit(*stack);
  visitor.visit(*newStack);

  // Compute expected contents of the stack. Regular pointers should be incremented. Internal
  // (frame) pointers should be incremented by the distance between the new and old stacks.
  vector<word_t> expected((startSp - endSp) / kWordSize);
  expected.assign(reinterpret_cast<word_t*>(stack->sp()),
                  reinterpret_cast<word_t*>(stack->sp()) + expected.size());
  for (word_t& i : expected) {
    if (i == kObjectMarker)
      i += 4;         // pointers
    else if (i > 100 && i != reinterpret_cast<word_t>(*function) && i != kNotSet)
      i += delta;  // fp in each frame
  }

  // Check the contents of the stack.
  word_t* contents = reinterpret_cast<word_t*>(newStack->sp());
  for (word_t i = 0; i < expected.size(); i++) {
    ASSERT_EQ(expected[i], contents[i]);
  }
}
