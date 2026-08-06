// Extracted from src/tokens.md:577
#![allow(unused)]
fn main() {
    let example = ("dog", "cat", "horse");
    let dog = example.0;
    let cat = example.1;
    // The following examples are invalid.
    let cat = example.01;  // ERROR no field named `01`
    let horse = example.0b10;  // ERROR no field named `0b10`
    let unicorn = example.0usize; // ERROR suffixes on a tuple index are invalid
    let underscore = example.0_0; // ERROR no field `0_0` on type `(&str, &str, &str)`
}
