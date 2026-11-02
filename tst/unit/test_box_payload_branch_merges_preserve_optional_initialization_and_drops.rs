//@ compile-variant: -O
use std::cell::Cell;
struct Mark<'a>(&'a Cell<u32>);
impl Drop for Mark<'_> { fn drop(&mut self) { self.0.set(self.0.get() + 1); } }
fn optional(first: bool, second: bool, log: &Cell<u32>) {
    let owner;
    if first { owner = Box::new(Mark(log)); drop(*owner); }
    else if second { owner = Box::new(Mark(log)); }
}
fn whole_or_inner(first: bool, second: bool, log: &Cell<u32>) {
    let owner = Box::new(Mark(log));
    if first { if second { drop(*owner); } else { drop(owner); } }
    else if second { drop(*owner); }
}
fn main() {
    for first in [false, true] {
        for second in [false, true] {
            let log = Cell::new(0);
            optional(std::hint::black_box(first), std::hint::black_box(second), &log);
            assert_eq!(log.replace(0), u32::from(first || second));
            whole_or_inner(std::hint::black_box(first), std::hint::black_box(second), &log);
            assert_eq!(log.get(), 1);
        }
    }
}
