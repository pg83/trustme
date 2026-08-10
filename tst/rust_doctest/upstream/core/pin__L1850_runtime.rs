// Extracted from library/core/src/pin.rs:1850
#![feature(coroutines)]
#![feature(coroutine_trait)]
use core::{
    ops::{Coroutine, CoroutineState},
    pin::pin,
};

fn coroutine_fn() -> impl Coroutine<Yield = usize, Return = ()> /* not Unpin */ {
 // Allow coroutine to be self-referential (not `Unpin`)
 // vvvvvv        so that locals can cross yield points.
    #[coroutine] static || {
        let foo = String::from("foo");
        let foo_ref = &foo; // ------+
        yield 0;                  // | <- crosses yield point!
        println!("{foo_ref}"); // <--+
        yield foo.len();
    }
}

fn main() {
    let mut coroutine = pin!(coroutine_fn());
    match coroutine.as_mut().resume(()) {
        CoroutineState::Yielded(0) => {},
        _ => unreachable!(),
    }
    match coroutine.as_mut().resume(()) {
        CoroutineState::Yielded(3) => {},
        _ => unreachable!(),
    }
    match coroutine.resume(()) {
        CoroutineState::Yielded(_) => unreachable!(),
        CoroutineState::Complete(()) => {},
    }
}
