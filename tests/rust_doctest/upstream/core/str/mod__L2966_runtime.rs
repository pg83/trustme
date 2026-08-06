// Extracted from library/core/src/str/mod.rs:2966
#![allow(unused)]
fn main() {
    for c in "❤\n!".escape_default() {
        print!("{c}");
    }
    println!();
}
