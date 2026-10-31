//@ aux-build: conditional_mixed_dependency_library.rs
extern crate conditional_mixed_dependency_library as library;
fn main() {
    assert_eq!(library::mixed(String::from("fir")), (String::from("fir"), 37, 48, 3));
    assert_eq!(library::mixed(43u16), (43, 37, 48, 3));
}
