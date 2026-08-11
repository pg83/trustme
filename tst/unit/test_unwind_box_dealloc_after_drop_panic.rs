use std::alloc::{GlobalAlloc, Layout, System};
use std::panic::{catch_unwind, AssertUnwindSafe};
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

struct PanicOnDrop([u8; 32]);

impl Drop for PanicOnDrop {
    fn drop(&mut self) {
        panic!("panic in boxed value drop");
    }
}

fn main() {
    let value = Box::new(PanicOnDrop([0; 32]));
    TARGET.store((&*value as *const PanicOnDrop).cast_mut().cast(), Ordering::SeqCst);

    let result = catch_unwind(AssertUnwindSafe(|| drop(value)));
    assert!(result.is_err());
    assert!(TARGET_DEALLOCATED.load(Ordering::SeqCst));
}
