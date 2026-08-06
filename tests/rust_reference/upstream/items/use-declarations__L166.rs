// Extracted from src/items/use-declarations.md:166
#![allow(unused)]
fn main() {
    // Creates bindings to:
    // - `std::collections::BTreeSet`
    // - `std::collections::hash_map`
    // - `std::collections::hash_map::HashMap`
    use std::collections::{BTreeSet, hash_map::{self, HashMap}};
}
