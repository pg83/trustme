//@ edition: 2021
//@ compile-only
//@ compile-flags: -Cemit-cpp-only -Copt-level=0 -Cpanic=unwind
//@ proc-macro-aux-build: cov99_w1419_future_tokens.rs

#![feature(async_drop)]
#![allow(incomplete_features, dead_code, non_camel_case_types)]

cov99_w1419_future_tokens::future_name! {
    use std::future::{async_drop_in_place, pending, AsyncDrop, Future};
    use std::mem::MaybeUninit;
    use std::pin::{pin, Pin};
    use std::task::{Context, Waker};

    struct Deferred;

    impl Drop for Deferred {
        fn drop(&mut self) {}
    }

    impl AsyncDrop for Deferred {
        async fn drop(self: Pin<&mut Self>) {
            pending::<()>().await;
        }
    }

    struct State(usize, Deferred);
    struct Placeholder(MaybeUninit<State>);

    extern "Rust" {
        fn cov99_w1419_may_unwind();
    }

    impl Drop for Placeholder {
        fn drop(&mut self) {
            let _guard = Deferred;
            unsafe { cov99_w1419_may_unwind(); }
        }
    }

    fn main() {
        let mut value = Placeholder(MaybeUninit::new(State(0, Deferred)));
        let mut dropping = pin!(unsafe { async_drop_in_place(&mut value) });
        let mut context = Context::from_waker(Waker::noop());
        let _ = dropping.as_mut().poll(&mut context);
    }
}
