// Extracted from src/lifetimes.md:40
#![allow(unused)]
fn main() {
    let x = 0;
    let y = &x;
    let z = &y;
}
