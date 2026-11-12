//@ input: support/metadata/own_mutable_reborrow_drops.metadata
//@ input: support/metadata/mutable_reborrow_drops.py
//@ compile-flags: --extern codec=$DIR/support/metadata/own_mutable_reborrow_drops.metadata -O

extern crate codec;

use std::cell::Cell;

struct Payload<'a>(&'a Cell<u32>, u16);

impl Drop for Payload<'_> {
    fn drop(&mut self) {
        self.0.set(self.0.get() + 1);
    }
}

fn main() {
    let dropped = Cell::new(0);
    let mut payload = Payload(&dropped, 89);
    let mut count = 0;
    codec::twice(&mut payload, &mut count);
    assert_eq!(count, 2 * std::mem::size_of_val(&payload));
    assert_eq!(dropped.get(), 0);
    payload.1 += 5;
    codec::twice(&mut (), &mut count);
    assert_eq!(count, 2 * std::mem::size_of_val(&payload));
    codec::twice(&mut payload, &mut count);
    assert_eq!(count, 4 * std::mem::size_of_val(&payload));
    assert_eq!(payload.1, 94);
    assert_eq!(dropped.get(), 0);
    drop(payload);
    assert_eq!(dropped.get(), 1);
}
