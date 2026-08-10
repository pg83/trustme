// Extracted from src/items/functions.md:137
#![allow(unused)]
fn main() {
    use std::fmt::Debug;
    
    fn foo<T>(x: &[T]) where T: Debug {
        // details elided
    }
    
    foo(&[1, 2]);
}
