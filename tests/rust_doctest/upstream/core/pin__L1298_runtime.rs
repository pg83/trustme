// Extracted from library/core/src/pin.rs:1298
#![allow(unused)]
fn main() {
    use std::pin::Pin;
    use std::task::Context;
    use std::future::Future;

    fn move_pinned_closure(mut x: impl Future, cx: &mut Context<'_>) {
        // Create a closure that moves `x`, and then internally uses it in a pinned way.
        let mut closure = move || unsafe {
            let _ignore = Pin::new_unchecked(&mut x).poll(cx);
        };
        // Call the closure, so the future can assume it has been pinned.
        closure();
        // Move the closure somewhere else. This also moves `x`!
        let mut moved = closure;
        // Calling it again means we polled the future from two different locations,
        // violating the pinning API contract.
        moved(); // Potential UB ⚠️
    }
}
