// Extracted from src/types/impl-trait.md:15
#![allow(unused)]
fn main() {
    trait Trait {}
    impl Trait for () {}
    
    // argument position: anonymous type parameter
    fn foo(arg: impl Trait) {
    }
    
    // return position: abstract return type
    fn bar() -> impl Trait {
    }
}
