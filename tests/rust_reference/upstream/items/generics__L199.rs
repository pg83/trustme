// Extracted from src/items/generics.md:199
#![allow(unused)]
fn main() {
    // ok
    struct Foo<const N: usize>;
    enum Bar<const M: usize> { A, B }
    
    // ERROR: unused parameter
    struct Baz<T>;
    struct Biz<'a>;
    struct Unconstrained;
    impl<const N: usize> Unconstrained {}
}
