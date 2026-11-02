//@ compile-fail: bound modifier `?` can only be applied to `Sized`

trait Unimplemented {}
fn value() -> impl ?Unimplemented + PartialEq<u8> { 29u8 }

fn main() {
    assert!(value() == 29u8);
    assert_eq!(std::mem::size_of_val(&value()), 1);
}
