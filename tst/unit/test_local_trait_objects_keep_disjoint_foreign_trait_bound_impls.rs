use std::fmt::Display;
use std::marker::PhantomData;

trait Local {}

trait Classify {
    fn tag() -> u8;
}

struct Slot<T: ?Sized>(PhantomData<T>);

impl Classify for Slot<dyn Local> {
    fn tag() -> u8 { 17 }
}

impl<T: ?Sized + Display> Classify for Slot<T> {
    fn tag() -> u8 { 29 }
}

fn main() {
    assert_eq!(<Slot<dyn Local> as Classify>::tag(), 17);
    assert_eq!(<Slot<u16> as Classify>::tag(), 29);
}
