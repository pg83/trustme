#pragma once

#include <memory>
#include <string>

class CodeGenerator;
class HIRCrate;
struct WireBoard;

// Debug backend: dumps the monomorphised MIR of every item instead of
// emitting C. Selected with `-C codegen-mode=monomir`.
extern ::std::unique_ptr<CodeGenerator> TransCodegenGetGeneratorMonoMir(const WireBoard& wb, const HIRCrate& crate, const ::std::string& outfile);
