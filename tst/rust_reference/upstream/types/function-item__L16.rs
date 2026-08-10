// Extracted from src/types/function-item.md:16
#![allow(unused)]
fn main() {
    fn foo<T>() { }
    let x = &mut foo::<i32>;
    *x = foo::<u32>; //~ ERROR mismatched types
}
