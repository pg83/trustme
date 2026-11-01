use std::cell::Cell;
struct Mark<'a>(&'a Cell<u16>, String);
impl Drop for Mark<'_> { fn drop(&mut self) { self.0.set(self.0.get() + 1); } }
struct Packet<'a> { first: Mark<'a>, second: Mark<'a> }

#[inline(never)]
fn take(value: Mark<'_>) -> usize { value.1.len() }

fn main() {
    let count = Cell::new(0);
    let packet = Packet {
        first: Mark(&count, String::from("first")),
        second: Mark(&count, String::from("second")),
    };
    let pair = (packet.first, packet.second);
    let first = pair.0;
    let forwarded = first;
    assert_eq!(take(forwarded), 5);
    assert_eq!(count.get(), 1);
    let second = pair.1;
    assert_eq!(take(second), 6);
    assert_eq!(count.get(), 2);
}
