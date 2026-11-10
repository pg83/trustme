//@ compile-flags: -O

use std::cell::Cell;

struct Recorded<'a>(&'a Cell<u32>);

impl Drop for Recorded<'_> {
    fn drop(&mut self) {
        self.0.set(self.0.get() + 1);
    }
}

#[inline(always)]
fn checked(value: u32) -> u32 {
    assert!(value != 0);
    value + 3
}

#[inline(always)]
extern "C" fn boundary(value: u32) -> u32 {
    checked(value)
}

fn exercise(log: &Cell<u32>, value: u32) -> u32 {
    let _record = Recorded(log);
    boundary(value)
}

fn main() {
    let log = Cell::new(0);
    assert_eq!(exercise(&log, std::hint::black_box(14)), 17);
    assert_eq!(exercise(&log, std::hint::black_box(26)), 29);
    assert_eq!(log.get(), 2);
}
