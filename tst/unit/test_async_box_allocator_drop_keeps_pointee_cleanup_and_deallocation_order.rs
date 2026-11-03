//@ input: aux/tuple_drop_log.rs
#![feature(async_drop, allocator_api)]
#[path = "aux/tuple_drop_log.rs"] mod log;
use std::alloc::{AllocError, Allocator, Global, Layout};
use std::cell::Cell;
use std::future::{poll_fn, AsyncDrop, Future};
use std::pin::{pin, Pin};
use std::ptr::NonNull;
use std::task::{Context, Poll, Waker};
struct Arena<'a> { order: &'a Cell<usize>, freed: &'a Cell<usize> }
unsafe impl Allocator for Arena<'_> {
    fn allocate(&self, layout: Layout) -> Result<NonNull<[u8]>, AllocError> { Global.allocate(layout) }
    unsafe fn deallocate(&self, pointer: NonNull<u8>, layout: Layout) {
        Global.deallocate(pointer, layout);
        self.freed.set(self.freed.get() + 1);
    }
}
impl Drop for Arena<'_> { fn drop(&mut self) { self.order.set(9999); } }
impl AsyncDrop for Arena<'_> {
    async fn drop(self: Pin<&mut Self>) {
        let mut paused = false;
        poll_fn(|_| { if paused { Poll::Ready(()) } else { paused = true; Poll::Pending } }).await;
        self.order.set(self.order.get() * 10 + 7);
    }
}
async fn finish(order: &Cell<usize>, freed: &Cell<usize>) {
    let value = Box::new_in((log::Immediate(order, 1), log::Deferred(order, 2), log::Immediate(order, 3)), Arena { order, freed });
    std::hint::black_box(&value);
}
fn main() {
    let order = Cell::new(0);
    let freed = Cell::new(0);
    let mut future = pin!(finish(&order, &freed));
    let mut context = Context::from_waker(Waker::noop());
    assert_eq!(future.as_mut().poll(&mut context), Poll::Pending);
    assert_eq!(order.get(), 99993);
    assert_eq!(freed.get(), 1);
    assert_eq!(future.as_mut().poll(&mut context), Poll::Ready(()));
    assert_eq!(order.get(), 999937);
}
