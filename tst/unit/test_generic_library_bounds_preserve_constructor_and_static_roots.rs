//@ aux-build: conditional_constructor_dependency_library.rs
extern crate conditional_constructor_dependency_library as library;
fn main() {
    let (value, flag, record, choice) = library::parts(41u16);
    assert_eq!(value, 41);
    assert_eq!(*flag, 37);
    assert_eq!(record(43).0, 43);
    match choice(47) { library::Choice::Packet(value) => assert_eq!(value, 47), library::Choice::Empty => panic!() }
}
