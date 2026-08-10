// Extracted from src/patterns.md:322
#![allow(unused)]
fn main() {
    let [mut x] = &[()]; //~ ERROR
    let [ref x] = &[()]; //~ ERROR
    let [ref mut x] = &mut [()]; //~ ERROR
}
