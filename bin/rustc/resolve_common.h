#pragma once

#include "tagged_union.h"

#include <stdexcept>

struct Span;
class ExpandProcMacro;
class MacroRules;

class ASTCrate;
struct Settings;
class ASTModule;
class ASTItem;
class ASTPath;

struct ASTAbsolutePath;

class HIRCrate;
class HIRModule;
class HIRProcMacro;
class HIRTypeItem;
class HIRValueItem;

// Definitions generated from resolve_common.tu.
#include "resolve_common_tu.h"

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
extern ResolveModuleRef ResolveLookupGetModule(const Span& span, const Settings& settings, const ASTCrate& crate, const ASTPath& basePath, ASTPath path, bool ignoreLast, ASTAbsolutePath* outPath);
extern ResolveItemRefMacro ResolveLookupMacro(const Span& span, const Settings& settings, const ASTCrate& crate, const ASTPath& basePath, ASTPath path, ASTAbsolutePath* outPath);
// Returns the module that contains the provided name
extern ResolveModuleRef ResolveLookupGetModuleForName(const Span& sp, const Settings& settings, const ASTCrate& crate, const ASTPath& basePath, const ASTPath& path, ResolveNamespace ns, ASTAbsolutePath* outPath);
