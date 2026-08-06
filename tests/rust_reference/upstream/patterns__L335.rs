// Extracted from src/patterns.md:335
#![allow(unused)]
fn main() {
    let [&x] = &[&()]; //~ ERROR
}
