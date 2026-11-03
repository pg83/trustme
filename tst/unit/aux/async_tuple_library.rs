#![feature(async_drop)]
#[path = "tuple_drop_log.rs"] mod log;
use std::cell::Cell;
use std::future::poll_fn;
use std::task::Poll;
pub async fn finish<T: Copy>(order: &Cell<usize>, seed: T, forget_first: bool) -> T {
    let value = (log::Deferred(order, 1), log::Immediate(order, 2), log::Deferred(order, 3));
    if forget_first { core::mem::forget(value.0); }
    let mut waiting = true;
    poll_fn(|_| { if waiting { waiting = false; Poll::Pending } else { Poll::Ready(()) } }).await;
    std::hint::black_box(&value.1);
    seed
}
