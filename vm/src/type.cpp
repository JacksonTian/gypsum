// Copyright 2014 Jay Conrod. All rights reserved.

// This file is part of CodeSwitch. Use of this source code is governed by
// the 3-clause BSD license that can be found in the LICENSE.txt file.


#include "type-inl.h"

namespace codeswitch {
namespace internal {

bool Type::isSubtypeOf(Type* other) {
  if (equals(other))
    return true;
  if (!isPrimitive() || !other->isPrimitive())
    return false;
  Class* clas = asClass();
  Class* otherClass = other->asClass();
  return clas->isSubclassOf(otherClass);
}


bool Type::equals(Type* other) {
  if (bitField() != other->bitField())
    return false;
  if (isClass()) {
    return asClass() == other->asClass();
  } else {
    return true;
  }
}

}
}
