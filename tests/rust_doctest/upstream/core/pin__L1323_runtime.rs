// Extracted from library/core/src/pin.rs:1323
#![allow(unused)]
fn main() {
    use std::pin::pin;
    use std::task::Context;
    use std::future::Future;
    
    fn move_pinned_closure(mut x: impl Future, cx: &mut Context<'_>) {
        let mut x = pin!(x);
        // Create a closure that captures `x: Pin<&mut _>`, which is safe to move.
        let mut closure = move || {
            let _ignore = x.as_mut().poll(cx);
        };
        // Call the closure, so the future can assume it has been pinned.
        closure();
        // Move the closure somewhere else.
        let mut moved = closure;
        // Calling it again here is fine (except that we might be polling a future that already
        // returned `Poll::Ready`, but that is a separate problem).
        moved();
    }
}
