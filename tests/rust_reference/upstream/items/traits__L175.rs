// Extracted from src/items/traits.md:175
#![allow(unused)]
fn main() {
    // `Self: Sized` traits are dyn-incompatible.
    trait TraitWithSize where Self: Sized {}
    
    struct S;
    impl TraitWithSize for S {}
    let obj: Box<dyn TraitWithSize> = Box::new(S); // ERROR
}
