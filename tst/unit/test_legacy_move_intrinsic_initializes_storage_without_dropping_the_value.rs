#![feature(intrinsics)]
#![allow(internal_features)]

use std::cell::Cell;
use std::mem::MaybeUninit;

extern "rust-intrinsic" {
    fn move_val_init<T>(destination: *mut T, value: T);
}

struct Item<'a> {
    drops: &'a Cell<u32>,
    value: u16,
}

impl<'a> Drop for Item<'a> {
    fn drop(&mut self) { self.drops.set(self.drops.get() + 1); }
}

fn main() {
    let drops = Cell::new(0);
    let mut storage = MaybeUninit::<Item<'_>>::uninit();
    unsafe { move_val_init(storage.as_mut_ptr(), Item { drops: &drops, value: 43 }); }
    assert_eq!(drops.get(), 0);
    let value = unsafe { storage.assume_init() };
    assert_eq!(value.value, 43);
    drop(value);
    assert_eq!(drops.get(), 1);

    let mut empty = MaybeUninit::<()>::uninit();
    unsafe { move_val_init(empty.as_mut_ptr(), ()); }
    assert_eq!(unsafe { empty.assume_init() }, ());
}
