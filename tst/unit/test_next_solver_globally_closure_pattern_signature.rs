//@ crate-type: lib
//@ compile-flags: -Znext-solver=globally

// Committing a next-solver response must not drop the inference effects of
// the selected impl's where-clauses: `impl<F: FnMut(char) -> bool> Pattern
// for F` seeds the closure's parameter type through the re-exported bound
// (the certainty-only nested evaluation cannot bind the closure signature).
// Mirrors rustc-demangle legacy.rs (`rest.find(|c| c == '$' || c == '.')`).

pub fn f(s: &str) -> Option<usize> {
    s.find(|c| c == '$' || c == '.')
}
