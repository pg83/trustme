// A closure's own argument type says what an opaque this body defines turns out
// to be: `run(|x: u32| .., 0)` where `run` wants `FnOnce(Input)`.
#![feature(type_alias_impl_trait)]

pub trait Anything {}
impl<T> Anything for T {}

pub type Input = impl Anything;

#[define_opaque(Input)]
fn bop(_: Input) {
    run(|x: u32| assert_eq!(x, 0), 0);
}

fn run<F: FnOnce(Input)>(f: F, i: Input) {
    f(i);
}

#[define_opaque(Input)]
fn make() -> Input {
    1u32
}

fn main() {
    bop(make());
}
