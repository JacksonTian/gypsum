#!/usr/bin/env python

# Copyright 2014 Jay Conrod. All rights reserved.

# This file is part of CodeSwitch. Use of this source code is governed by
# the 3-clause BSD license that can be found in the LICENSE.txt file.


import os.path
import sys
import yaml


class Counter(object):
    def __init__(self):
        self.n = -1

    def __call__(self):
        n = self.n
        self.n -= 1
        return n

    def value(self):
        return self.n


if len(sys.argv) != 3:
    sys.stderr.write("usage: %0 builtins.yaml builtins.h\n" % sys.argv[0])
    sys.exit(1)


builtinsYamlFileName = sys.argv[1]
builtinsHFileName = sys.argv[2]

with open(builtinsYamlFileName) as builtinsYamlFile:
    classesData, functionsData = yaml.load_all(builtinsYamlFile.read())


with open(builtinsHFileName, "w") as builtinsHFile:
    nextClassId = Counter()
    builtinsHFile.write("""// DO NOT MODIFY
// This file was automatically generated by gen_builtins_h.py

#ifndef builtins_h
#define builtins_h

#include "utils.h"

namespace codeswitch {
namespace internal {

typedef i64 BuiltinId;

inline bool isBuiltinId(i64 id) {
  return id < 0;
}


inline word_t builtinIdToIndex(BuiltinId id) {
  return static_cast<word_t>(~id);
}

// Builtin class and type ids.
""")
    lastClassId = None
    lastTypeId = None
    for classData in classesData:
        lastTypeId = nextClassId()
        builtinsHFile.write("const BuiltinId %s = %d;\n" % (classData["id"], lastTypeId))
        if not classData["isPrimitive"]:
            lastClassId = lastTypeId
    builtinsHFile.write("""
const BuiltinId LAST_BUILTIN_CLASS_ID = %d;
const word_t BUILTIN_CLASS_COUNT = static_cast<word_t>(-LAST_BUILTIN_CLASS_ID);
const BuiltinId LAST_BUILTIN_TYPE_ID = %d;
const word_t BUILTIN_TYPE_COUNT = static_cast<word_t>(-LAST_BUILTIN_TYPE_ID);

""" % (lastClassId, lastTypeId))

    nextFunctionId = Counter()
    builtinsHFile.write("// Builtin function ids.\n")
    for classData in classesData:
        if not classData["isPrimitive"]:
            for ctorData in classData["constructors"]:
                lastFunctionId = nextFunctionId()
                builtinsHFile.write("const BuiltinId %s = %d;\n" %
                                        (ctorData["id"], lastFunctionId))
        for methodData in classData["methods"]:
            lastFunctionId = nextFunctionId()
            builtinsHFile.write("const BuiltinId %s = %d;\n" %
                                    (methodData["id"], lastFunctionId))
    for functionData in functionsData:
        lastFunctionId = nextFunctionId()
        builtinsHFile.write("const BuiltinId %s = %d;\n" %
                                (functionData["id"], lastFunctionId))
    builtinsHFile.write("""
const BuiltinId LAST_BUILTIN_FUNCTION_ID = %d;
const word_t BUILTIN_FUNCTION_COUNT = static_cast<word_t>(-LAST_BUILTIN_FUNCTION_ID);

}
}

#endif
""" % lastFunctionId)
