//@ input: support/metadata/own_shared_reborrow_drops.metadata
//@ input: support/metadata/shared_reborrow_drops.py
//@ input: support/metadata/own_equivalent_mir_blocks.metadata
//@ input: support/metadata/equivalent_mir_blocks.py
//@ compile-flags: --extern codec=$DIR/support/metadata/own_shared_reborrow_drops.metadata -O

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
    let payload = Payload(&dropped, 73);
    let mut count = 0;
    for flag in [std::hint::black_box(false), std::hint::black_box(true)] {
        codec::borrowed(&payload, &mut count, flag);
        assert_eq!(dropped.get(), 0);
        assert_eq!(payload.1, 73);
    }
    assert_eq!(count, 2 * std::mem::size_of_val(&payload));
    codec::borrowed(&(), &mut count, std::hint::black_box(false));
    assert_eq!(count, 2 * std::mem::size_of_val(&payload));
    drop(payload);
    assert_eq!(dropped.get(), 1);
}
