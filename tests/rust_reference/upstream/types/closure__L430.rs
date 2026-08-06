// Extracted from src/types/closure.md:430
#![allow(unused)]
fn main() {
    let x: [u8; 1] = [0];
    let c = || match x { // Does not capture `x`.
        [_] => (), // Length is fixed.
    };
    x; // OK: `x` can be moved here.
    c();
}
