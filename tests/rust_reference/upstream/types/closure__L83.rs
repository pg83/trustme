// Extracted from src/types/closure.md:83
#![allow(unused)]
fn main() {
    let x = [0; 1024];
    let c = || {
        let y = x; // x captured by ImmBorrow
    };
}
