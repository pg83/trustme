// Extracted from src/types/impl-trait.md:42
#![allow(unused)]
fn main() {
    trait Trait {}
    
    // generic type parameter
    fn with_generic_type<T: Trait>(arg: T) {
    }
    
    // impl Trait in argument position
    fn with_impl_trait(arg: impl Trait) {
    }
}
