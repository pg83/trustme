// Extracted from src/paths.md:305
#![allow(unused)]
fn main() {
    trait T {
        type Item;
        const C: i32;
        // `Self` will be whatever type that implements `T`.
        fn new() -> Self;
        // `Self::Item` will be the type alias in the implementation.
        fn f(&self) -> Self::Item;
    }
    struct S;
    impl T for S {
        type Item = i32;
        const C: i32 = 9;
        fn new() -> Self {           // `Self` is the type `S`.
            S
        }
        fn f(&self) -> Self::Item {  // `Self::Item` is the type `i32`.
            Self::C                  // `Self::C` is the constant value `9`.
        }
    }
    
    // `Self` is in scope within the generics of a trait definition,
    // to refer to the type being defined.
    trait Add<Rhs = Self> {
        type Output;
        // `Self` can also reference associated items of the
        // type being implemented.
        fn add(self, rhs: Rhs) -> Self::Output;
    }
    
    struct NonEmptyList<T> {
        head: T,
        // A struct can reference itself (as long as it is not
        // infinitely recursive).
        tail: Option<Box<Self>>,
    }
}
