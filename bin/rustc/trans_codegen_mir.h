#pragma once

#include <memory>
#include <string>

class CodeGenerator;
class HIRCrate;
struct WireBoard;

std::unique_ptr<CodeGenerator> TransCodegenGetGeneratorMonoMir(const WireBoard& wb, const HIRCrate& crate, const std::string& outfile);
