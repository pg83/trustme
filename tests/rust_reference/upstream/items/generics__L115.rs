// Extracted from src/items/generics.md:115
#![allow(unused)]
fn main() {
    // Examples where const parameters may not be used.
    
    // Not allowed to combine in other expressions in types, such as the
    // arithmetic expression in the return type here.
    fn bad_function<const N: usize>() -> [u8; {N + 1}] {
        // Similarly not allowed for array repeat expressions.
        [1; {N + 1}]
    }
}
