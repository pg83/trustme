//@ check-pass
//@ compile-flags: -Znext-solver

#![feature(type_alias_impl_trait)]

type Tait = impl Iterator<Item = impl Sized>;

fn mk<T>() -> T {
    todo!()
}

#[define_opaque(Tait)]
fn define_tait_through_projection_relation() {
    let x: Tait = mk();
    let mut array = mk();
    let mut iter = IntoIterator::into_iter(array);
    iter = x;
    array = [0i32; 32];
}

fn main() {}
