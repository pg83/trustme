use std::alloc::{GlobalAlloc, Layout, System};
use std::ptr;
use std::sync::atomic::{AtomicBool, AtomicPtr, Ordering};

struct TrackingAllocator;

static TARGET: AtomicPtr<u8> = AtomicPtr::new(ptr::null_mut());
static TARGET_DEALLOCATED: AtomicBool = AtomicBool::new(false);

unsafe impl GlobalAlloc for TrackingAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        System.alloc(layout)
    }

    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        if ptr == TARGET.load(Ordering::SeqCst) {
            TARGET_DEALLOCATED.store(true, Ordering::SeqCst);
        }
        System.dealloc(ptr, layout);
    }
}

#[global_allocator]
static ALLOCATOR: TrackingAllocator = TrackingAllocator;

fn main() {
    let value = Box::new([0_u8; 64]);
    TARGET.store(value.as_ptr().cast_mut(), Ordering::SeqCst);
    drop(value);
    assert!(TARGET_DEALLOCATED.load(Ordering::SeqCst));
}
