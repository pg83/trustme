#pragma once

#include <stdexcept>
#include "tagged_union.h"

struct Span;
class ExpandProcMacro;
class MacroRules;

    class ASTCrate;
    class ASTModule;
    class ASTItem;
    class ASTPath;

    struct ASTAbsolutePath;

namespace HIR {
    class Crate;
    class Module;
    class ProcMacro;
    class TypeItem;
    class ValueItem;
};

TAGGED_UNION(ResolveModuleRef, None, (None, struct {}), (ImplicitPrelude, struct {}), (Ast, const ASTModule*), (Hir, const HIR::Module*));

TAGGED_UNION(ResolveItemRefMacro, None, (None, struct {}), (InternalMacro, ExpandProcMacro*), (ProcMacro, const HIR::ProcMacro*), (MacroRules, const MacroRules*));
TAGGED_UNION(ResolveItemRefType, None, (None, struct {}), (Ast, const ASTItem*), (Hir, const HIR::TypeItem*), (HirRoot, const HIR::Crate*), (AstRoot, const ASTModule*));
TAGGED_UNION(ResolveItemRefValue, None, (None, struct {}), (Ast, const ASTItem*), (Hir, const HIR::ValueItem*));

TAGGED_UNION(ResolveItemRef, None, (None, struct {}), (Namespace, ResolveItemRefType), (Value, ResolveItemRefValue), (Macro, ResolveItemRefMacro));

enum class ResolveNamespace {
    Namespace,
    Value,
    Macro,
};
extern ::std::ostream& operator<<(::std::ostream& os, ResolveNamespace ns);

/// <summary>
/// Obtain a reference to the module pointed to by `path` (relative to `base_path`)
/// </summary>
/// <param name="span"></param>
/// <param name="crate"></param>
/// <param name="base_path"></param>
/// <param name="path"></param>
/// <param name="ignore_last">Ignore the last node of the path</param>
/// <param name="out_path"></param>
/// <returns></returns>
extern ResolveModuleRef ResolveLookupGetModule(const Span& span, const ASTCrate& crate, const ASTPath& basePath, ASTPath path, bool ignoreLast, ASTAbsolutePath* outPath);
extern ResolveItemRefMacro ResolveLookupMacro(const Span& span, const ASTCrate& crate, const ASTPath& basePath, ASTPath path, ASTAbsolutePath* outPath);
// Returns the module that contains the provided name
extern ResolveModuleRef ResolveLookupGetModuleForName(const Span& sp, const ASTCrate& crate, const ASTPath& basePath, const ASTPath& path, ResolveNamespace ns, ASTAbsolutePath* outPath);
