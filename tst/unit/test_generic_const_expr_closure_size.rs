#![feature(generic_const_exprs)]

trait True<const VALUE: bool> {}

impl True<true> for () {}

trait ZeroSized {}

impl<T> ZeroSized for T
where
    (): True<{ core::mem::size_of::<T>() == 0 }>,
{}

fn zero_sized_closure() -> impl ZeroSized {
    || {}
}

fn zero_sized_closure_with_local() -> impl ZeroSized {
    || {
        let value = 1;
        value
    }
}

fn main() {
    let captured = 1;
    let closure = || captured;
    assert!(core::mem::size_of_val(&closure) > 0);
}
