// Extracted from src/macros-by-example.md:345
#![allow(unused)]
fn main() {
    // Implicit visibility is `pub(crate)`.
    macro_rules! private_m {
        () => {};
    }
    
    // Implicit visibility is `pub`.
    #[macro_export]
    macro_rules! pub_m {
        () => {};
    }
    
    pub(crate) use private_m as private_macro; // OK.
    pub use pub_m as pub_macro; // OK.
    
    pub use private_m; // ERROR: `private_m` is only public within
                       // the crate and cannot be re-exported outside.
}
