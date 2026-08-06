// Extracted from library/core/src/task/ready.rs:11
#![allow(unused)]
fn main() {
    use std::task::{ready, Context, Poll};
    use std::future::{self, Future};
    use std::pin::Pin;
    
    pub fn do_poll(cx: &mut Context<'_>) -> Poll<()> {
        let mut fut = future::ready(42);
        let fut = Pin::new(&mut fut);
    
        let num = ready!(fut.poll(cx));
        let _ = num;
        // ... use num
    
        Poll::Ready(())
    }
}
