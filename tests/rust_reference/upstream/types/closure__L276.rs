// Extracted from src/types/closure.md:276
#![allow(unused)]
fn main() {
    let x: u8;
    let c = || {
        let _ = x; // ERROR: Binding `x` isn't initialized.
    };
}
