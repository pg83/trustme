//@ compile-fail: bound modifier `?` can only be applied to `Sized`

fn value() -> impl ?Copy + PartialEq<u16> { 319u16 }

fn main() {
    assert!(value() == 319u16);
    assert_eq!(std::mem::size_of_val(&value()), 2);
}
