// Extracted from library/core/src/iter/traits/iterator.rs:758
#![allow(unused)]
#![allow(unused_must_use)]
fn main() {
    // don't do this:
    (0..5).map(|x| println!("{x}"));
    
    // it won't even execute, as it is lazy. Rust will warn you about this.
    
    // Instead, use a for-loop:
    for x in 0..5 {
        println!("{x}");
    }
}
