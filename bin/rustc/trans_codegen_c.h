#pragma once

#include <memory>
#include <string>

class CodeGenerator;
class HIRCrate;
struct WireBoard;

// The C backend: translates monomorphised MIR into a single C source
// file and drives the platform C compiler over it.
extern ::std::unique_ptr<CodeGenerator> TransCodegenGetGeneratorC(const WireBoard& wb, const HIRCrate& crate, const ::std::string& outfile);
