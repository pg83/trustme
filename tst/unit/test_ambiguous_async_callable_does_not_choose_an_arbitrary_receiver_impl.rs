//@ compile-fail: error
#![feature(async_fn_traits, unboxed_closures)]
use std::future::{ready, Ready};
use std::marker::PhantomData;
use std::ops::AsyncFnOnce;

struct Callable<T>(PhantomData<T>);
macro_rules! callable {
    ($type:ty) => {
        impl AsyncFnOnce<(u8,)> for Callable<$type> {
            type Output = u8;
            type CallOnceFuture = Ready<u8>;
            extern "rust-call" fn async_call_once(self, args: (u8,)) -> Ready<u8> {
                ready(args.0)
            }
        }
    };
}
callable!(u8);
callable!(u16);

fn main() {
    let callable = Callable::<_>(PhantomData);
    let future = callable(7u8);
    drop(future);
}
