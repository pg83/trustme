// Extracted from library/core/src/char/methods.rs:437
#![allow(unused)]
fn main() {
    for c in '❤'.escape_unicode() {
        print!("{c}");
    }
    println!();
}
