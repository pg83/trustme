//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(tuple_trait, type_alias_impl_trait, unboxed_closures)]

trait Callable: FnOnce<Self::Args, Output = u32> {
    type Args: std::marker::Tuple;
}

impl<F: FnOnce<(u32,), Output = u32>> Callable for F {
    type Args = (u32,);
}

type Callback = impl Callable<Args = impl std::marker::Tuple>;

fn identity<T>(value: T) -> T {
    value
}

#[define_opaque(Callback)]
fn callback() -> Callback {
    identity::<Callback>(|value: u32| value)
}

fn main() {
    let _ = callback();
}
