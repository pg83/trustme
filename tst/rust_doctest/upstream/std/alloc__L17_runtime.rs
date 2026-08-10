// Extracted from library/std/src/alloc.rs:17
use std::alloc::{GlobalAlloc, System, Layout};

struct MyAllocator;

unsafe impl GlobalAlloc for MyAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        unsafe { System.alloc(layout) }
    }

    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        unsafe { System.dealloc(ptr, layout) }
    }
}

#[global_allocator]
static GLOBAL: MyAllocator = MyAllocator;

fn main() {
    // This `Vec` will allocate memory through `GLOBAL` above
    let mut v = Vec::new();
    v.push(1);
}
