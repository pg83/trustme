// Extracted from src/expressions.md:287
#![allow(unused)]
fn main() {
    // This call doesn't create an internal temporary.
    let x = { let x = format_args!("{}", 0); x };
    x; // OK
}
