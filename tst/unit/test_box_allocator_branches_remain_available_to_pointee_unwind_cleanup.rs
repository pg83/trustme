//@ default-compile-flags: -O
#![feature(allocator_api)]
use std::alloc::{AllocError, Allocator, Global, Layout};
use std::cell::Cell;
use std::ptr::NonNull;

struct Arena<'a> {
    result: &'a Cell<u32>,
    outer: bool,
    inner: bool,
    tag: u8,
}
unsafe impl Allocator for Arena<'_> {
    fn allocate(&self, layout: Layout) -> Result<NonNull<[u8]>, AllocError> {
        Global.allocate(layout)
    }
    #[inline(always)]
    unsafe fn deallocate(&self, pointer: NonNull<u8>, layout: Layout) {
        if self.outer {
            if self.inner { self.result.set(11); } else { self.result.set(13); }
        } else {
            match self.tag {
                0 => self.result.set(17),
                1 => self.result.set(19),
                _ => self.result.set(23),
            }
        }
        Global.deallocate(pointer, layout);
    }
}
struct Payload(bool);
impl Drop for Payload {
    #[inline(never)]
    fn drop(&mut self) {
        if self.0 { panic!("pointee drop failed"); }
    }
}
fn main() {
    for (outer, inner, tag, expected) in [
        (true, true, 0, 11), (true, false, 0, 13),
        (false, false, 0, 17), (false, false, 1, 19), (false, false, 2, 23),
    ] {
        let result = Cell::new(0);
        let value = Box::new_in(Payload(false), Arena { result: &result, outer, inner, tag });
        drop(std::hint::black_box(value));
        assert_eq!(result.get(), expected);
    }
}
