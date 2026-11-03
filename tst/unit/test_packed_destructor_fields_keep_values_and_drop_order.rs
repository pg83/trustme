use std::cell::Cell;

struct Recorded<'a>(&'a Cell<u64>, u64);
impl Drop for Recorded<'_> {
    #[inline(never)]
    fn drop(&mut self) { self.0.set(self.0.get() * 10 + self.1); }
}
#[repr(C, packed)]
struct Packed<'a> { tag: u8, first: Recorded<'a>, second: Recorded<'a> }
struct Holder<'a> { before: Recorded<'a>, packed: Packed<'a>, after: Recorded<'a> }

fn main() {
    let events = Cell::new(0);
    let value = Holder {
        before: Recorded(&events, 1),
        packed: Packed { tag: 19, first: Recorded(&events, 2), second: Recorded(&events, 3) },
        after: Recorded(&events, 4),
    };
    assert_eq!(value.packed.tag, 19);
    drop(value);
    assert_eq!(events.get(), 1234);
}
