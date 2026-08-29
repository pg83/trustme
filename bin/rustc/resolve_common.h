#pragma once

#include <iosfwd>

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

#include "resolve_common_tu.h"

enum class ResolveNamespace {
    Namespace,
    Value,
    Macro,
};
ResolveModuleRef ResolveLookupGetModule(const Span& span, const Settings& settings, const ASTCrate& crate, const ASTPath& basePath, ASTPath path, bool ignoreLast, ASTAbsolutePath* outPath);
ResolveItemRefMacro ResolveLookupMacro(const Span& span, const Settings& settings, const ASTCrate& crate, const ASTPath& basePath, ASTPath path, ASTAbsolutePath* outPath);

ResolveModuleRef ResolveLookupGetModuleForName(const Span& sp, const Settings& settings, const ASTCrate& crate, const ASTPath& basePath, const ASTPath& path, ResolveNamespace ns, ASTAbsolutePath* outPath);
