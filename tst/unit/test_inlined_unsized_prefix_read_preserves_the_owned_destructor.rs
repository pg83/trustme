//@ compile-flags: -O
#![feature(unsized_fn_params)]
#![allow(incomplete_features)]

use std::cell::Cell;

#[repr(C)]
struct Packet<'a, T: ?Sized> {
    drops: &'a Cell<u16>,
    tag: u16,
    tail: T,
}

impl<T: ?Sized> Drop for Packet<'_, T> {
    fn drop(&mut self) {
        self.drops.set(self.drops.get() + 1);
    }
}

#[inline(always)]
fn consume(value: Packet<'_, [u8]>) -> u16 {
    value.tag
}

#[inline(never)]
fn read(value: Box<Packet<'_, [u8]>>) -> u16 {
    consume(*value)
}

fn main() {
    let drops = Cell::new(0);
    let value: Box<Packet<'_, [u8]>> = Box::new(Packet {
        drops: &drops,
        tag: std::hint::black_box(479u16),
        tail: [3u8, 5],
    });
    assert_eq!(read(value), 479);
    assert_eq!(drops.get(), 1);
}
