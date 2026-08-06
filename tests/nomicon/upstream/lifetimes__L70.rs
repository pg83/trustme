// Extracted from src/lifetimes.md:70
#![allow(unused)]
fn main() {
    let x = 0;
    let z;
    let y = &x;
    z = y;
}
