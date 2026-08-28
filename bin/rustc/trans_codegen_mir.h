#pragma once

#include <memory>
#include <string>

class CodeGenerator;
class HIRCrate;
struct WireBoard;

extern std::unique_ptr<CodeGenerator> TransCodegenGetGeneratorMonoMir(const WireBoard& wb, const HIRCrate& crate, const std::string& outfile);
