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
    fn projected<C: Carrier>(self: C::Handle) -> u16 { C::unpack(self) }
}

fn main() {
    assert_eq!(Counter::projected::<Boxed>(Box::new(Counter(17))), 17);
    assert_eq!(Box::new(Counter(23)).projected::<Boxed>(), 23);
}
