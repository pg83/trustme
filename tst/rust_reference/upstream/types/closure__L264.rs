// Extracted from src/types/closure.md:264
#![allow(unused)]
fn main() {
    struct S; // A non-`Copy` type.
    let mut x = [S, S];
    let c = || {
        let [x0, _] = x; // Captures all of `x` by `ByValue`.
    };
    let _ = &mut x[1]; // ERROR: Borrow of moved value.
}
