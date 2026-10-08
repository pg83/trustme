//@ aux-build: async_drop_dependency.rs
// rustc elaborates a drop inside a coroutine as an async drop only when the
// crate being compiled enables `async_drop` (`build_drop` and
// `destructor_call_block` in elaborate_drop.rs check
// `tcx.features().async_drop()`). Without the feature, a type from another
// crate that implements `AsyncDrop` gets its sync `Drop` (rustc's
// async-drop/dependency-dropped.rs, revision without_feature). We built the
// async drop whatever the crate's features were.
extern crate async_drop_dependency;

use async_drop_dependency::{MongoDrop, ASYNC_DROPS, SYNC_DROPS};
use std::future::Future;
use std::pin::pin;
use std::sync::atomic::Ordering;
use std::task::{Context, Poll, Waker};

async fn asyncdrop() {
    let _ = MongoDrop::new().await;
}

fn block_on<T>(fut: impl Future<Output = T>) -> T {
    let mut fut = pin!(fut);
    let ctx = &mut Context::from_waker(Waker::noop());
    loop {
        match fut.as_mut().poll(ctx) {
            Poll::Pending => {}
            Poll::Ready(t) => break t,
        }
    }
}

fn main() {
    block_on(asyncdrop());
    assert_eq!(ASYNC_DROPS.load(Ordering::SeqCst), 0);
    assert_eq!(SYNC_DROPS.load(Ordering::SeqCst), 1);
}
