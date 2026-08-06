// Extracted from src/attributes/limits.md:37
#![allow(unused)]
#![recursion_limit = "1"]
fn main() {
    
    // This fails because it requires two recursive steps to auto-dereference.
    (|_: &u8| {})(&&&1);
}
