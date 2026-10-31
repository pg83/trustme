#![feature(core_intrinsics)]
use std::cell::Cell;
struct Value<'a>(&'a Cell<u32>, u32);
impl Drop for Value<'_> { fn drop(&mut self) { self.0.set(self.0.get() + self.1); } }
fn main() {
    let total = Cell::new(0);
    let mut storage = std::mem::MaybeUninit::uninit();
    unsafe { std::intrinsics::write_via_move(storage.as_mut_ptr(), Value(&total, 37)); }
    let value = unsafe { storage.assume_init() };
    assert_eq!(total.get(), 0);
    drop(value);
    assert_eq!(total.get(), 37);
}
