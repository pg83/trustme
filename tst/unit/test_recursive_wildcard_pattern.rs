#![feature(coroutine_trait, coroutines, impl_trait_in_assoc_type)]

use std::ops::Coroutine;

trait Provider: Sized {
    type Coro: Coroutine<(), Return = (), Yield = ()>;

    fn start(context: Context<Self>) -> Self::Coro;
}

struct Context<P: Provider> {
    coroutine: Box<P::Coro>,
}

impl Provider for () {
    type Coro = impl Coroutine<(), Return = (), Yield = ()>;

    fn start(context: Context<Self>) -> Self::Coro {
        #[coroutine]
        move || {
            match context {
                _ => (),
            }
            yield ();
        }
    }
}

fn main() {}
