//@ aux-build: conditional_trait_dependency_library.rs
extern crate conditional_trait_dependency_library as library;
fn main() {
    assert_eq!(library::available(String::from("fir")), (String::from("fir"), 44));
    assert_eq!(library::available(41u16), (41, 44));
}
