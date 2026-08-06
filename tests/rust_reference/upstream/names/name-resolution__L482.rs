// Extracted from src/names/name-resolution.md:482
#![allow(unused)]
fn main() {
    macro_rules! define_ambig {
        () => {
            mod ambig {
                pub struct Name;
            }
        }
    }
    
    mod ambig {
        pub struct Name;
    }
    
    const _: () = {
        // Introduce innermost candidate for
        // `ambig` mod in this macro expansion.
        define_ambig!();
        use ambig::Name; // ERROR: `ambig` is ambiguous.
    };
}
