//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

pub trait Projection {
    type Output;
    fn project(self) -> Self::Output;
}

fn project<T: Projection>(value: T) -> T::Output {
    value.project()
}

pub fn forward<T: Projection>(value: T) -> T::Output {
    project(value)
}
