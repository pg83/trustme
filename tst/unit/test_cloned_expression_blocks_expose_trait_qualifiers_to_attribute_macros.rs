//@ proc-macro-aux-build: inspect_trait_qualifiers.rs
#![feature(auto_traits)]
macro_rules! twice { ($body:expr) => { ($body, $body) }; }
fn main() {
    let unsafe_pair = twice!({
        #[inspect_trait_qualifiers::qualifiers]
        unsafe trait Required {}
        QUALIFIERS
    });
    let auto_pair = twice!({
        #[inspect_trait_qualifiers::qualifiers]
        unsafe auto trait Flag {}
        QUALIFIERS
    });
    assert_eq!(unsafe_pair, (1, 1));
    assert_eq!(auto_pair, (3, 3));
}
