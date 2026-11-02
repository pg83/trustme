//@ compile-variant: -O
#![feature(allocator_api)]
use std::alloc::{AllocError, Allocator, Global, Layout};
use std::cell::Cell;
use std::ptr::NonNull;
struct Tracked<'a>(&'a Cell<u32>);
unsafe impl Allocator for Tracked<'_> {
    fn allocate(&self, layout: Layout) -> Result<NonNull<[u8]>, AllocError> { Global.allocate(layout) }
    unsafe fn deallocate(&self, pointer: NonNull<u8>, layout: Layout) {
        self.0.set(self.0.get() + 10);
        Global.deallocate(pointer, layout);
    }
}
impl Drop for Tracked<'_> { fn drop(&mut self) { self.0.set(self.0.get() + 1); } }
struct Payload<'a>(&'a Cell<u32>, u8);
impl Drop for Payload<'_> { fn drop(&mut self) { self.0.set(self.0.get() + 100); } }
fn main() {
    for moved in [false, true] {
        let events = Cell::new(0);
        {
            let owner = Box::new_in(Payload(&events, 17), Tracked(&events));
            if std::hint::black_box(moved) { let value = *owner; assert_eq!(value.1, 17); drop(value); }
            else { drop(owner); }
        }
        assert_eq!(events.get(), 111);
    }
}
