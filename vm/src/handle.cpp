// Copyright 2014 Jay Conrod. All rights reserved.

// This file is part of CodeSwitch. Use of this source code is governed by
// the 3-clause BSD license that can be found in the LICENSE.txt file.


#include "handle.h"

#include "block.h"
#include "error.h"
#include "vm.h"

using namespace std;

namespace codeswitch {
namespace internal {

HandleStorage::HandleStorage()
    : canCreateLocal_(false) { }


HandleStorage* HandleStorage::fromBlock(Block* block) {
  return fromVM(block->getVM());
}


HandleStorage* HandleStorage::fromVM(VM* vm) {
  return &vm->handleStorage();
}


Block** HandleStorage::createLocal(Block* block) {
  ASSERT(canCreateLocal_);
  localSlots_.push_back(block);
  return &localSlots_.back();
}


void HandleStorage::createPersistent(Block* block, Block*** out_slot, size_t* out_index) {
  if (persistentFreeList_.empty()) {
    *out_index = persistentSlots_.size();
    persistentSlots_.push_back(block);
    *out_slot = &persistentSlots_.back();
  } else {
    *out_index = persistentFreeList_.back();
    persistentFreeList_.pop_back();
    *out_slot = &persistentSlots_[*out_index];
  }
  **out_slot = block;
}


void HandleStorage::destroyPersistent(size_t index) {
  ASSERT(index < persistentSlots_.size());
  persistentSlots_[index] = nullptr;
  persistentFreeList_.push_back(index);
}


HandleStorage::iterator::iterator(const deque<Block*>::iterator it,
                                  bool isLocal,
                                  HandleStorage* storage)
    : it_(it), isLocal_(isLocal), storage_(storage) {
  if (isLocal_ && it_ == storage_->localSlots_.end()) {
    isLocal_ = false;
    it_ = storage_->persistentSlots_.begin();
  }
  while (!isValid() && !done())
    advance();
}


HandleStorage::iterator& HandleStorage::iterator::operator ++ () {
  do {
    advance();
  } while (!isValid() && !done());
  return *this;
}


void HandleStorage::iterator::advance() {
  it_++;
  if (isLocal_ && it_ == storage_->localSlots_.end()) {
    isLocal_ = false;
    it_ = storage_->persistentSlots_.begin();
  }
}


bool HandleStorage::iterator::isValid() const {
  return *it_ != nullptr;
}


bool HandleStorage::iterator::done() const {
  return it_ == storage_->persistentSlots_.end();
}


HandleStorage::iterator HandleStorage::begin() {
  return iterator(localSlots_.begin(), true, this);
}


HandleStorage::iterator HandleStorage::end() {
  return iterator(persistentSlots_.end(), false, this);
}


HandleScope::HandleScope(HandleStorage* storage)
    : storage_(storage),
      oldCanCreateLocal_(storage->canCreateLocal_),
      oldSize_(storage->localSlots_.size()),
      escapeSlot_(nullptr),
      escapeSlotUsed_(false) {
  storage_->canCreateLocal_ = true;
  escapeSlot_ = storage_->createLocal(nullptr);
}


HandleScope::HandleScope(VM* vm)
    : HandleScope(&vm->handleStorage()) { }


HandleScope::~HandleScope() {
  storage_->localSlots_.resize(oldSize_);
  storage_->canCreateLocal_ = oldCanCreateLocal_;
}

}
}
