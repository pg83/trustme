// Extracted from library/core/src/async_iter/mod.rs:33
#![allow(unused)]
fn main() {
    use core::task::{Context, Poll};
    use core::pin::Pin;
    trait AsyncIterator {
        type Item;
        fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>>;
    }
}
