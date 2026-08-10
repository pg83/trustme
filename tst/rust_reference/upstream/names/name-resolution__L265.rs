// Extracted from src/names/name-resolution.md:265
#![allow(unused)]
fn main() {
    mod glob {
        pub mod ambig {
            pub struct Name;
        }
    }
    
    // Outer `ambig` candidate.
    pub mod ambig {
        pub struct Name;
    }
    
    const _: () = {
        // Cannot resolve `ambig` through this glob
        // because of the outer `ambig` candidate above.
        use glob::*;
        use ambig::Name; // ERROR: `ambig` is ambiguous.
    };
}
