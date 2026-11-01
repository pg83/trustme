//@ compile-variant: -O
use std::cell::Cell;
use std::mem::ManuallyDrop;

struct Guard<'a>(&'a Cell<u16>, u16);
impl Drop for Guard<'_> {
    fn drop(&mut self) { self.0.set(self.0.get() + self.1); }
}
#[inline(never)]
unsafe fn destroy<T>(value: *mut T) { std::ptr::drop_in_place(value); }
#[inline(never)]
unsafe fn indirectly_destroy<'a>(value: *mut Guard<'a>) {
    let callback: unsafe fn(*mut Guard<'a>) = std::ptr::drop_in_place;
    callback(value);
}
fn main() {
    let events = Cell::new(0);
    let mut first = ManuallyDrop::new(Guard(&events, 7));
    unsafe { destroy(&mut *first); }
    assert_eq!(events.get(), 7);
    let mut second = ManuallyDrop::new(Guard(&events, 11));
    unsafe { indirectly_destroy(&mut *second); }
    assert_eq!(events.get(), 18);
}
