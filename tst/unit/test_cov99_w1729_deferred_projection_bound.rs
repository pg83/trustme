//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

pub trait Projection {
    type Output;
}

fn same_output<L: Projection, R: Projection<Output = L::Output>>(_: L, _: R) {}

pub fn forward<T: Projection>(left: T, right: T) {
    same_output(left, right);
}
