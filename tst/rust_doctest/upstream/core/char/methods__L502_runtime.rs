// Extracted from library/core/src/char/methods.rs:502
#![allow(unused)]
fn main() {
    for c in '\n'.escape_debug() {
        print!("{c}");
    }
    println!();
}
