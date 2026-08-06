// Extracted from src/types/closure.md:250
#![allow(unused)]
fn main() {
    struct S; // A non-`Copy` type.
    let x = (S, S);
    let c = || {
        let (x0, ..) = x;  // Captures `x.0` by `ByValue`.
    };
    // Only the first tuple field was captured by the closure.
    x.1; // OK: `x.1` can be moved here.
    c();
}
