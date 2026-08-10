// Extracted from src/names/name-resolution.md:177
#![allow(unused)]
fn main() {
    mod m1 {
        pub struct Ambig;
    }
    
    mod m2 {
        pub struct Ambig;
    }
    
    use m1::*;
    use m2::*; // OK: No name conflict.
    const _: () = {
        // This is permitted, since resolution is not through the
        // ambiguous globs.
        struct Ambig;
        let x = Ambig; // OK.
    };
}
