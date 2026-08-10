// Extracted from src/items/traits.md:184
#![allow(unused)]
fn main() {
    // Dyn-incompatible if `Self` is a type argument.
    trait Super<A> {}
    trait WithSelf: Super<Self> where Self: Sized {}
    
    struct S;
    impl<A> Super<A> for S {}
    impl WithSelf for S {}
    let obj: Box<dyn WithSelf> = Box::new(S); // ERROR: cannot use `Self` type parameter
}
