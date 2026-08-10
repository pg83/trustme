// Extracted from src/items/traits.md:156
#![allow(unused)]
fn main() {
    use std::rc::Rc;
    // Examples of dyn-incompatible traits.
    trait DynIncompatible {
        const CONST: i32 = 1;  // ERROR: cannot have associated const
    
        fn foo() {}  // ERROR: associated function without Sized
        fn returns(&self) -> Self; // ERROR: Self in return type
        fn typed<T>(&self, x: T) {} // ERROR: has generic type parameters
        fn nested(self: Rc<Box<Self>>) {} // ERROR: nested receiver cannot be dispatched on
    }
    
    struct S;
    impl DynIncompatible for S {
        fn returns(&self) -> Self { S }
    }
    let obj: Box<dyn DynIncompatible> = Box::new(S); // ERROR
}
