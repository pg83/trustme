//@ compile-flags: -Cpanic=unwind
//@ compile-variant: -O
use std::cell::Cell;
struct Mark<'a>(u32, &'a Cell<u32>);
impl Drop for Mark<'_> { fn drop(&mut self) { self.1.set(self.1.get() + self.0); } }
enum Parcel<'a> { One(Mark<'a>), Pair(Mark<'a>, Mark<'a>) }
#[inline(never)]
fn visit(count: &Cell<u32>) { count.set(count.get() + 19); }
#[inline(never)]
fn work(flag: bool, count: &Cell<u32>, callback: fn(&Cell<u32>)) {
    let parcel = if flag { Parcel::One(Mark(23, count)) }
                 else { Parcel::Pair(Mark(29, count), Mark(31, count)) };
    let mut guard = Mark(37, count);
    callback(count);
    guard = Mark(41, count);
    match parcel {
        Parcel::One(value) => { drop(value); callback(count); }
        Parcel::Pair(first, _second) => { drop(first); callback(count); }
    }
    drop(guard);
}
fn main() {
    for flag in [false, true] {
        let count = Cell::new(0);
        work(std::hint::black_box(flag), &count, std::hint::black_box(visit));
        assert_eq!(count.get(), if flag { 139 } else { 176 });
    }
}
