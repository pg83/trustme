use std::cell::Cell;

struct Recorded<'a>(&'a Cell<u32>, u32);
impl Drop for Recorded<'_> {
    #[inline(never)]
    fn drop(&mut self) { self.0.set(self.0.get() * 10 + self.1); }
}
trait Value { fn value(&self) -> u32; }
impl Value for Recorded<'_> { fn value(&self) -> u32 { self.1 } }
struct Tail<'a, T: ?Sized> { first: Recorded<'a>, tail: T }

fn main() {
    let events = Cell::new(0);
    let slice: Box<Tail<'_, [Recorded<'_>]>> = Box::new(Tail {
        first: Recorded(&events, 1),
        tail: [Recorded(&events, 2), Recorded(&events, 3)],
    });
    assert_eq!(slice.tail.len(), 2);
    drop(slice);
    assert_eq!(events.get(), 123);
    events.set(0);
    let object: Box<Tail<'_, dyn Value + '_>> = Box::new(Tail {
        first: Recorded(&events, 4),
        tail: Recorded(&events, 5),
    });
    assert_eq!(object.tail.value(), 5);
    drop(object);
    assert_eq!(events.get(), 45);
}
