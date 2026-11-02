//@ compile-variant: -O

fn value() -> &'static (impl ?Sized + PartialEq<u16>) { &319u16 }

fn main() {
    assert!(value().eq(&319u16));
    assert_eq!(std::mem::size_of_val(value()), 2);
}
