//@ compile-flags: -Zdump-ast

use std::ops::{Range, RangeInclusive};

fn closed(value: &mut u16, range: RangeInclusive<u16>) -> &mut u16 {
    assert_eq!((*range.start(), *range.end()), (1, 3));
    value
}

fn open(value: &mut u16, range: Range<u16>) -> &mut u16 {
    assert_eq!((range.start, range.end), (1, 3));
    value
}

struct Counter(u16);

impl Counter {
    fn slot(&mut self, range: RangeInclusive<u16>) -> &mut u16 {
        closed(&mut self.0, range)
    }
}

struct Record<'a> {
    value: u16,
    total: &'a mut u16,
}

impl Drop for Record<'_> {
    fn drop(&mut self) {
        *self.total += self.value;
    }
}

fn main() {
    let mut first = 7u16;
    let mut second = 11u16;
    *closed(&mut first, 1..=3) += 5;
    *open(&mut second, 1..3) = 23;
    let pointer: fn(&mut u16, RangeInclusive<u16>) -> &mut u16 = closed;
    *(&pointer)(&mut first, 1..=3) += 7;
    let mut counter = Counter(29);
    *counter.slot(1..=3) += 2;
    let mut total = 0u16;
    (Record { value: 37, total: &mut total }, 1..=3).0.value += 4;
    assert_eq!((first, second, counter.0, total), (19, 23, 31, 41));
}
