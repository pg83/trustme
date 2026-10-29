//@ edition: 2021
#![feature(async_drop, allocator_api)]
#![allow(incomplete_features)]
use std::alloc::{AllocError, Allocator, Global, Layout};
use std::future::{AsyncDrop, Future};
use std::pin::{pin, Pin};
use std::ptr::NonNull;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::task::{Context, Poll, Waker};
static FREED: AtomicUsize = AtomicUsize::new(0);
static ASYNC: AtomicUsize = AtomicUsize::new(0);
static SYNC: AtomicUsize = AtomicUsize::new(0);
struct Arena;
unsafe impl Allocator for Arena {
    fn allocate(&self, layout: Layout) -> Result<NonNull<[u8]>, AllocError> {
        Global.allocate(layout)
    }
    unsafe fn deallocate(&self, pointer: NonNull<u8>, layout: Layout) {
        Global.deallocate(pointer, layout);
        FREED.fetch_add(1, Ordering::SeqCst);
    }
}
impl Drop for Arena {
    fn drop(&mut self) { SYNC.fetch_add(1, Ordering::SeqCst); }
}
impl AsyncDrop for Arena {
    async fn drop(self: Pin<&mut Self>) {
        let mut waiting = true;
        std::future::poll_fn(|cx| {
            if waiting {
                waiting = false;
                cx.waker().wake_by_ref();
                Poll::Pending
            } else { Poll::Ready(()) }
        }).await;
        ASYNC.fetch_add(1, Ordering::SeqCst);
    }
}
async fn conditional(active: bool) {
    let boxed;
    if active {
        boxed = Box::new_in(String::from("value"), Arena);
        let moved = *boxed;
        assert_eq!(moved, "value");
    }
}
fn main() {
    let mut context = Context::from_waker(Waker::noop());
    for active in [false, true] {
        let mut future = pin!(conditional(active));
        if active {
            assert_eq!(future.as_mut().poll(&mut context), Poll::Pending);
            assert_eq!(FREED.load(Ordering::SeqCst), 1);
            assert_eq!(ASYNC.load(Ordering::SeqCst), 0);
        }
        assert_eq!(future.as_mut().poll(&mut context), Poll::Ready(()));
        assert_eq!(FREED.load(Ordering::SeqCst), active as usize);
        assert_eq!(ASYNC.load(Ordering::SeqCst), active as usize);
        assert_eq!(SYNC.load(Ordering::SeqCst), 0);
    }
}
