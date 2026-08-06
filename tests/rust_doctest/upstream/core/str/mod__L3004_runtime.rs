// Extracted from library/core/src/str/mod.rs:3004
#![allow(unused)]
fn main() {
    for c in "❤\n!".escape_unicode() {
        print!("{c}");
    }
    println!();
}
