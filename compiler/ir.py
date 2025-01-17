# Copyright 2014, Jay Conrod. All rights reserved.
#
# This file is part of Gypsum. Use of this source code is governed by
# the GPL license that can be found in the LICENSE.txt file.


from bytecode import *
from data import *
from ir_types import *

import StringIO

class Package(object):
    def __init__(self):
        self.globals = []
        self.functions = []
        self.classes = []
        self.typeParameters = []
        self.strings = []
        self.entryFunction = -1

    def __str__(self):
        buf = StringIO.StringIO()
        for g in self.globals:
            buf.write("%s\n\n\n\n" % g)
        for f in self.functions:
            buf.write("%s\n\n" % f)
        for c in self.classes:
            buf.write("%s\n\n" % c)
        for p in self.typeParameters:
            buf.write("%s\n\n" % p)
        buf.write("entry function: %d\n" % self.entryFunction)
        return buf.getvalue()

    def addFunction(self, function):
        assert not hasattr(function, "id")
        function.id = len(self.functions)
        self.functions.append(function)
        return function.id

    def addClass(self, clas):
        assert not hasattr(clas, "id")
        clas.id = len(self.classes)
        self.classes.append(clas)
        return clas.id

    def addGlobal(self, gbl):
        assert not hasattr(gbl, "id")
        gbl.id = len(self.globals)
        self.globals.append(gbl)
        return gbl.id

    def addTypeParameter(self, tp):
        assert not hasattr(tp, "id")
        tp.id = len(self.typeParameters)
        self.typeParameters.append(tp)
        return tp.id

    def findOrAddString(self, s):
        assert type(s) == unicode
        for i in xrange(0, len(self.strings)):
            if self.strings[i] == s:
                return i
        self.strings.append(s)
        return len(self.strings) - 1

    def findFunction(self, **kwargs):
        return next(self.find(self.functions, kwargs))

    def findClass(self, **kwargs):
        return next(self.find(self.classes, kwargs))

    def findGlobal(self, **kwargs):
        return next(self.find(self.globals, kwargs))

    def findTypeParameter(self, **kwargs):
        return next(self.find(self.typeParameters, kwargs))

    def find(self, defns, kwargs):
        def matchItem(defn, key, value):
            if key == "clas":
                return isinstance(defn, Function) and \
                       hasattr(defn, "clas") and \
                       defn.clas is value
            elif key == "pred":
                return value(defn)
            else:
                return getattr(defn, key) == value
        def matchAll(defn):
            return all(matchItem(defn, k, v) for k, v in kwargs.iteritems())
        return (d for d in defns if matchAll(d))


class IrDefinition(Data):
    def isBuiltin(self):
        return self.id < 0

    def isTypeDefn(self):
        return False


class Global(IrDefinition):
    propertyNames = ("name", "type", "value", "flags")


class Function(IrDefinition):
    propertyNames = ("name", "returnType", "typeParameters", "parameterTypes",
                     "variables", "blocks", "flags")

    def canCallWith(self, typeArgs, argTypes):
        if len(self.typeParameters) != len(typeArgs) or \
           not all(arg.isSubtypeOf(param.upperBound) and param.lowerBound.isSubtypeOf(arg) \
                   for param, arg in zip(self.typeParameters, typeArgs)):
            return False

        if len(self.parameterTypes) != len(argTypes):
            return False
        if self.isMethod():
            # Nullable receivers are fine, since they are checked when a method is called.
            argTypes = [argTypes[0].withoutFlag(NULLABLE_TYPE_FLAG)] + argTypes[1:]
        paramTypes = [pt.substitute(self.typeParameters, typeArgs)
                      for pt in self.parameterTypes]
        return all(at.isSubtypeOf(pt) for at, pt in zip(argTypes, paramTypes))

    def isMethod(self):
        return hasattr(self, "clas")

    def isConstructor(self):
        return hasattr(self, "clas") and \
               isinstance(self.clas.constructors, list) and \
               any(ctor is self for ctor in self.clas.constructors)

    def isFinal(self):
        return not self.isMethod() or \
               self.isConstructor() or \
               hasattr(self.clas, "isPrimitive") and self.clas.isPrimitive

    def mayOverride(self, other):
        assert self.isMethod() and other.isMethod()
        typeParametersAreCompatible = \
            len(self.typeParameters) == len(other.typeParameters) and \
            all(atp.isEquivalent(btp) for atp, btp in
                zip(self.typeParameters, other.typeParameters))
        parameterTypesAreCompatible = \
            len(self.parameterTypes) == len(other.parameterTypes) and \
            all(bt.isSubtypeOf(at) for at, bt in
                zip(self.parameterTypes[1:], other.parameterTypes[1:]))
        returnTypeIsCompatible = self.returnType.isSubtypeOf(other.returnType)
        return typeParametersAreCompatible and \
               parameterTypesAreCompatible and \
               returnTypeIsCompatible

    def __repr__(self):
        return "Function(%s, %s, %s, %s, %s, %s, %s)" % \
            (self.name, repr(self.returnType), repr(self.typeParameters),
             repr(self.parameterTypes), repr(self.variables), repr(self.blocks),
             repr(self.flags))

    def __str__(self):
        buf = StringIO.StringIO()
        buf.write("%s def %s#%d" % (" ".join(self.flags), self.name, self.id))
        if self.typeParameters is not None and len(self.typeParameters) > 0:
            buf.write("[%s]" % ", ".join([str(tp) for tp in self.typeParameters]))
        if self.parameterTypes is not None and len(self.parameterTypes) > 0:
            buf.write("(%s)" % ", ".join([str(pt) for pt in self.parameterTypes]))
        if self.returnType is not None:
            buf.write(": " + str(self.returnType))
        if self.variables is not None and len(self.variables) > 0 or \
           self.blocks is not None and len(self.blocks) > 0:
            buf.write(" =\n")
        if self.variables is not None:
            for v in self.variables:
                buf.write("  var %s: %s (%s)\n" % (v.name, v.type, v.kind))
        if self.blocks is not None:
            for block in self.blocks:
                buf.write("%d:\n" % block.id)
                for inst in block.instructions:
                    buf.write("  %s\n" % inst)
        return buf.getvalue()


class Class(IrDefinition):
    propertyNames = ("name", "typeParameters", "supertypes", \
                     "initializer", "constructors", "fields", "methods", "flags")

    def superclass(self):
        assert self is not getNothingClass()
        if len(self.supertypes) == 0:
            return None
        else:
            return self.supertypes[0].clas

    def superclasses(self):
        """Returns a generator of superclasses in depth-first order, including this class."""
        nothingClassId = -2   # avoid circular import dependency with builtins
        assert self.id is not nothingClassId
        yield self
        clas = self
        while len(clas.supertypes) > 0:
            clas = clas.supertypes[0].clas
            yield clas

    def isSubclassOf(self, other):
        nothingClassId = -2   # avoid circular import dependency with builtins
        if self is other or self.id == nothingClassId:
            return True
        elif other.id == nothingClassId:
            return False
        else:
            return other in self.superclasses()

    def findCommonBaseClass(self, other):
        """Returns a class which (a) is a superclass of both classes, and (b) has no subclasses
        which are superclasses of both classes."""
        if self is other:
            return self
        selfBases = list(self.superclasses())
        selfLast = -len(selfBases) - 1
        otherBases = list(other.superclasses())
        otherLast = -len(otherBases) - 1
        i = -1
        while i > selfLast and i > otherLast and selfBases[i] is otherBases[i]:
            i -= 1
        return selfBases[i + 1]

    def getConstructor(self, argTypes):
        # TODO: support constructor overloading
        assert len(self.constructors) <= 1
        if len(self.constructors) > 0:
            return self.constructors[0]
        else:
            return None

    def getMethod(self, name, typeArgs=None, argTypes=None):
        assert (typeArgs is None) == (argTypes is None)
        candidate = None
        for m in self.methods:
            if m.name == name and \
               (argTypes is None or m.canCallWith(type[ClassType(self)] + argTypes)):
                assert candidate is None
                candidate = m
        return candidate

    def getMethodDict(self):
        methodDict = {}
        for i, m in enumerate(self.methods):
            if m.name not in methodDict:
                methodDict[m.name] = []
            methodDict[m.name].append((i, m))
        return methodDict

    def getField(self, name):
        for f in self.fields:
            if f.name == name:
                return f
        return None

    def getMember(self, name):
        for m in self.methods:
            if m.name == name:
                return m
        for f in self.fields:
            if f.name == name:
                return f
        return None

    def getMethodIndex(self, method):
        for i in xrange(len(self.methods)):
            if self.methods[i] is method:
                return i
        raise KeyError("method does not belong to this class")

    def isTypeDefn(self):
        return True

    def __repr__(self):
        return "Class(%s, %s, %s, %s, %s, %s, %s, %s)" % \
            (self.name, repr(self.typeParameters), repr(self.supertypes),
             repr(self.initializer), repr(self.constructors),
             repr(self.fields), repr(self.methods), repr(self.flags))

    def __str__(self):
        buf = StringIO.StringIO()
        buf.write("%s class %s#%d" % (" ".join(self.flags), self.name, self.id))
        buf.write("\n")
        for field in self.fields:
            buf.write("  %s\n" % str(field))
        if self.initializer is not None:
            buf.write("  %s\n" % str(self.initializer))
        for ctor in self.constructors:
            buf.write("  constructor #%d\n" % ctor.id)
        for method in self.methods:
            buf.write("  method #%d\n" % method.id)
        return buf.getvalue()


class TypeParameter(IrDefinition):
    propertyNames = ("name", "upperBound", "lowerBound", "flags")

    def isEquivalent(self, other):
        return self.upperBound == other.upperBound and \
               self.lowerBound == other.lowerBound

    def isTypeDefn(self):
        return True

    def __repr__(self):
        return "TypeParameter(%s, %s, %s, %s)" % \
            (self.name, self.upperBound, self.lowerBound, self.flags)

    def __str__(self):
        return "%s type %s#%d <: %s >: %s\n" % \
            (" ".join(self.flags), self.name, self.id, self.upperBound, self.lowerBound)


# List of variable kinds
LOCAL = "local"
PARAMETER = "parameter"

Variable = Data.makeClass("Variable", ("name", "type", "kind", "flags"))
Field = Data.makeClass("Field", ("name", "type", "flags"))
