#![feature(allocator_api)]
use std::alloc::{AllocError, Allocator, Global, Layout};
use std::cell::Cell;
use std::ptr::NonNull;

struct Event<'a>(&'a Cell<u32>, u32);
impl Drop for Event<'_> {
    #[inline(never)]
    fn drop(&mut self) { self.0.set(self.0.get() * 10 + self.1); }
}

#[inline(never)]
fn observe(events: &Cell<u32>) { std::hint::black_box(events.get()); }

struct Arena<'a>(&'a Cell<u32>);
unsafe impl Allocator for Arena<'_> {
    fn allocate(&self, layout: Layout) -> Result<NonNull<[u8]>, AllocError> {
        Global.allocate(layout)
    }
    #[inline(always)]
    unsafe fn deallocate(&self, pointer: NonNull<u8>, layout: Layout) {
        let _last = Event(self.0, 3);
        self.0.set(self.0.get() * 10 + 2);
        observe(self.0);
        Global.deallocate(pointer, layout);
    }
}
impl Drop for Arena<'_> {
    #[inline(always)]
    fn drop(&mut self) { self.0.set(self.0.get() * 10 + 4); }
}

fn main() {
    let events = Cell::new(0);
    let boxed = Box::new_in(Event(&events, 1), Arena(&events));
    drop(boxed);
    assert_eq!(events.get(), 1234);
}
