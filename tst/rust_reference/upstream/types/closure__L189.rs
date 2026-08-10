// Extracted from src/types/closure.md:189
#![allow(unused)]
fn main() {
    struct S; // A non-`Copy` type.
    let x = S;
    let c = || {
        let _ = x;  // Does not capture `x`.
    };
    let c = || match x {
        _ => (), // Does not capture `x`.
    };
    x; // OK: `x` can be moved here.
    c();
}
