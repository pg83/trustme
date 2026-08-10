// Extracted from library/core/src/error.rs:124
#![allow(unused)]
fn main() {
    if let Err(e) = "xc".parse::<u32>() {
        // Print `e` itself, no need for description().
        eprintln!("Error: {e}");
    }
}
