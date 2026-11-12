#![feature(arbitrary_self_types)]

use std::ops::Receiver;

struct Counter(u16);
trait Carrier {
    type Handle: Receiver<Target = Counter>;
    fn unpack(handle: Self::Handle) -> u16;
}
struct Boxed;
impl Carrier for Boxed {
    type Handle = Box<Counter>;
    fn unpack(handle: Self::Handle) -> u16 { handle.0 }
}
impl Counter {
    fn wrapped<C: Carrier>(self: Box<C::Handle>) -> u16 { C::unpack(*self) }
}

fn main() {
    assert_eq!(Counter::wrapped::<Boxed>(Box::new(Box::new(Counter(31)))), 31);
    assert_eq!(Box::new(Box::new(Counter(37))).wrapped::<Boxed>(), 37);
}
