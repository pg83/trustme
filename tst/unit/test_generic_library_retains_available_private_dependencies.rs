//@ aux-build: conditional_dependency_library.rs
extern crate conditional_dependency_library as library;
fn main() {
    assert_eq!(library::plain(String::from("fir")), (String::from("fir"), 37));
    assert_eq!(library::associated(43u16), (43, 41));
    assert_eq!(library::with_unsized(47u16, "elm"), (47, 3));
}
