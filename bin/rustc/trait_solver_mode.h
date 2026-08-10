#pragma once

struct TraitSolverConfig {
    // rustc 1.90 uses the new solver for coherence by default, while ordinary
    // type checking keeps the legacy solver unless explicitly requested.
    bool coherence = true;
    bool globally = false;
};

extern TraitSolverConfig gTraitSolverConfig;
