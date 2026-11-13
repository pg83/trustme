//@ compile-fail: error:0:

use core::cell::UnsafeCell;

struct Slot {
    value: UnsafeCell<u8>,
}

const ROOT: Slot = make_slot();

const fn make_slot() -> Slot {
    let borrowed = &ROOT;
    Slot {
        value: UnsafeCell::new(unsafe { *borrowed.value.get() }),
    }
}

fn main() {}
