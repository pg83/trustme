//@ crate-type: lib

#![feature(const_async_blocks, coroutine_trait, coroutines, rustc_attrs)]
#![feature(type_alias_impl_trait)]

use std::ops::Coroutine;

type Coro<Y, R> = impl Coroutine<Yield = Y, Return = R>;

#[define_opaque(Coro)]
const fn make<Y, R>(yielded: Y, returned: R) -> Coro<Y, R> {
    #[coroutine]
    move || {
        yield yielded;
        returned
    }
}

const _: Coro<usize, usize> = make(1, 2);
