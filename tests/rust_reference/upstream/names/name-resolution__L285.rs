// Extracted from src/names/name-resolution.md:285
#![allow(unused)]
fn main() {
    // As above, but with macros.
    pub mod m {
        macro_rules! f {
            () => {};
        }
        pub(crate) use f;
    }
    pub mod glob {
        macro_rules! f {
            () => {};
        }
        pub(crate) use f as ambig;
    }
    
    use m::f as ambig;
    
    const _: () = {
        use glob::*;
        ambig!(); // ERROR: `ambig` is ambiguous.
    };
}
