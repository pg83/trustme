// Extracted from src/types.md:111
#![allow(unused)]
fn main() {
    use std::any::Any;
    type T<'a> = &'a (dyn Any + Send);
}
