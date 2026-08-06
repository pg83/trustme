// Extracted from src/items/functions.md:128
#![allow(unused)]
fn main() {
    use std::fmt::Debug;
    fn foo<T>(x: T) where T: Debug {
    }
}
