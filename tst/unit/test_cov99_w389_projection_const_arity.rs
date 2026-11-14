//@ crate-type: lib
//@ compile-flags: --emit=metadata

pub trait Shapes {
    type With<const N: usize>;
    type Without;
}

struct Wrap<T>(T);

trait Select<T, const KIND: usize> {}

impl<T: Shapes> Select<T, 0> for Wrap<T::With<1>> {}
impl<T: Shapes> Select<T, 1> for Wrap<T::Without> {}

fn require<T: Shapes, W: Select<T, 1>>() {}

pub fn exercise<T: Shapes>() {
    require::<T, Wrap<T::Without>>();
}
