#![feature(allocator_api)]
use std::alloc::{AllocError, Allocator, Global, Layout};
use std::cell::Cell;
use std::ptr::NonNull;

struct Payload<'a>(&'a Cell<u32>, u32);
impl Drop for Payload<'_> {
    fn drop(&mut self) { self.0.set(self.0.get() * 10 + self.1); }
}
struct Arena<'a>(&'a Cell<u32>);
unsafe impl Allocator for Arena<'_> {
    fn allocate(&self, layout: Layout) -> Result<NonNull<[u8]>, AllocError> { Global.allocate(layout) }
    unsafe fn deallocate(&self, pointer: NonNull<u8>, layout: Layout) {
        self.0.set(self.0.get() * 10 + 2);
        Global.deallocate(pointer, layout);
    }
}
impl Drop for Arena<'_> {
    fn drop(&mut self) { self.0.set(self.0.get() * 10 + 3); }
}

fn main() {
    let events = Cell::new(0);
    let escaped = {
        let owner = Box::new_in(Payload(&events, 1), Arena(&events));
        *owner
    };
    assert_eq!(events.get(), 23);
    assert_eq!(escaped.1, 1);
    drop(escaped);
    assert_eq!(events.get(), 231);
}
