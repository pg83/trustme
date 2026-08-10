// Extracted from src/macros-by-example.md:329
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
}
