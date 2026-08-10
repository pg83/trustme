// Extracted from library/std/src/keyword_docs.rs:232
#![allow(unused)]
fn main() {
    // Printing odd numbers by skipping even ones
    for number in 1..=10 {
        if number % 2 == 0 {
            continue;
        }
        println!("{number}");
    }
}
