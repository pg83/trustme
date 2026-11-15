//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(tuple_trait, unboxed_closures)]

trait Callable: FnOnce<Self::Args, Output = u32> {
    type Args: std::marker::Tuple;
}

impl<F: FnOnce<(u32,), Output = u32>> Callable for F {
    type Args = (u32,);
}

fn callback() -> impl Callable<Args = impl std::marker::Tuple> {
    |value: u32| value
}

fn main() {
    let _ = callback();
}
