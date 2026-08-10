// Extracted from library/core/src/str/mod.rs:2920
#![allow(unused)]
fn main() {
    for c in "❤\n!".escape_debug() {
        print!("{c}");
    }
    println!();
}
