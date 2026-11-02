//@ compile-variant: -O
const UNIT: () = { let _ = 17u16; };
const VALUE: &u16 = { { &*(&23u16) } };
fn main() {
    assert_eq!(UNIT, ());
    assert_eq!(*VALUE, 23);
}
