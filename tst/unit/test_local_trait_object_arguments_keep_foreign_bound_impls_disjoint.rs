use std::marker::PhantomData;

trait Local {}

trait Classify {
    fn tag() -> u8;
}

struct Slot<T: ?Sized>(PhantomData<T>);

impl Classify for Slot<dyn Local> {
    fn tag() -> u8 { 43 }
}

impl<T: ?Sized> Classify for Slot<T>
where u16: PartialEq<T> {
    fn tag() -> u8 { 47 }
}

fn main() {
    assert_eq!(<Slot<dyn Local> as Classify>::tag(), 43);
    assert_eq!(<Slot<u16> as Classify>::tag(), 47);
}
