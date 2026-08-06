// Extracted from library/core/src/task/ready.rs:30
#![allow(unused)]
fn main() {
    use std::task::{Context, Poll};
    use std::future::{self, Future};
    use std::pin::Pin;
    
    pub fn do_poll(cx: &mut Context<'_>) -> Poll<()> {
        let mut fut = future::ready(42);
        let fut = Pin::new(&mut fut);
        
    let num = match fut.poll(cx) {
        Poll::Ready(t) => t,
        Poll::Pending => return Poll::Pending,
    };
        let _ = num; // to silence unused warning
        // ... use num
        
        Poll::Ready(())
    }
}
