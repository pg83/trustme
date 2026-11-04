//@ compile-flags: -Cpanic=unwind
//@ compile-variant: -O
use std::cell::Cell;
struct Mark<'a>(u32, &'a Cell<u32>);
impl Drop for Mark<'_> { fn drop(&mut self) { self.1.set(self.1.get() + self.0); } }
struct Packet<'a> { first: Mark<'a>, second: Mark<'a>, third: Mark<'a> }
#[inline(never)]
fn add(count: &Cell<u32>, value: u32) { count.set(count.get() + value); }
#[inline(never)]
fn work(flag: bool, count: &Cell<u32>, callback: fn(&Cell<u32>, u32)) {
    let packet = Packet { first: Mark(3, count), second: Mark(5, count), third: Mark(7, count) };
    callback(count, 11);
    if flag { drop(packet.first); callback(count, 13); }
    else { drop(packet.second); callback(count, 17); }
    drop(packet.third);
    assert_eq!(count.get(), if flag { 34 } else { 40 });
}
fn main() {
    for flag in [false, true] {
        let count = Cell::new(0);
        work(std::hint::black_box(flag), &count, std::hint::black_box(add));
        assert_eq!(count.get(), if flag { 39 } else { 43 });
    }
}
