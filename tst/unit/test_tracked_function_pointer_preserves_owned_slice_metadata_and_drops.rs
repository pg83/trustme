//@ compile-flags: -Zmir-opt-level=0
#![feature(unsized_fn_params)]

use std::cell::Cell;

struct Entry<'a> {
    number: u16,
    drops: &'a Cell<u16>,
}

impl Drop for Entry<'_> {
    fn drop(&mut self) {
        self.drops.set(self.drops.get() + 1);
    }
}

#[track_caller]
#[inline(never)]
fn consume(entries: [Entry<'_>], extra: u16) -> (u16, bool) {
    (
        entries[0].number + entries[entries.len() - 1].number + extra,
        std::panic::Location::caller().file() == file!(),
    )
}

fn main() {
    let drops = Cell::new(0);
    let entries: Box<[Entry<'_>]> = Box::new([
        Entry { number: 17, drops: &drops },
        Entry { number: 23, drops: &drops },
        Entry { number: 31, drops: &drops },
    ]);
    let indirect: fn([Entry<'_>], u16) -> (u16, bool) = consume;
    let indirect = std::hint::black_box(indirect);
    assert_eq!(indirect(*entries, 13), (61, true));
    assert_eq!(drops.get(), 3);
}
