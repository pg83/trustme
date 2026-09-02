#pragma once

#include <std/sys/types.h>

namespace stl {
    class ObjPool;
}

struct ASTCrate;
class HIRCrate;
class HIRTypeInterner;
class HIRInherentCache;
class ExpandRegistry;
class LangItems;
struct Settings;
struct TargetSpec;
struct NextSolverCrateCache;
struct HIRMutableOwnerCache;

struct WireBoard {
    struct TargetLayoutContext;
    struct MirOperationsContext;
    struct CtfeContext;
    struct ManglingContext;

    explicit WireBoard(stl::ObjPool* pool);

    stl::ObjPool* pool = nullptr;

    stl::ObjPool* astPool = nullptr;

    mutable u32 id = 0;

    HIRTypeInterner* types = nullptr;
    ExpandRegistry* expandRegistry = nullptr;

    Settings* settings = nullptr;

    const TargetSpec* target = nullptr;
    TargetLayoutContext* targetLayouts = nullptr;
    MirOperationsContext* mirOperations = nullptr;
    CtfeContext* ctfe = nullptr;
    ManglingContext* mangling = nullptr;

    ASTCrate* astCrate = nullptr;

    HIRCrate* crate = nullptr;

    LangItems* langItems = nullptr;

    HIRInherentCache* inherentMethods = nullptr;

    NextSolverCrateCache* solverCache = nullptr;

    HIRMutableOwnerCache* hirOwners = nullptr;
};
