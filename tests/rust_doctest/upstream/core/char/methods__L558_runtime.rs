// Extracted from library/core/src/char/methods.rs:558
#![allow(unused)]
fn main() {
    for c in '"'.escape_default() {
        print!("{c}");
    }
    println!();
}
