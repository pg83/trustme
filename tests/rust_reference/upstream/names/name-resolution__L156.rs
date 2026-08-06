// Extracted from src/names/name-resolution.md:156
#![allow(unused)]
fn main() {
    mod m1 {
        pub struct Ambig;
    }
    
    mod m2 {
        pub struct Ambig;
    }
    
    // OK: This brings conficting names in the same namespace into scope
    // but they have not been used yet.
    use m1::*;
    use m2::*;
    
    const _: () = {
        // The error happens when the name with the conflicting candidates
        // is used.
        let x = Ambig; // ERROR: `Ambig` is ambiguous.
    };
}
