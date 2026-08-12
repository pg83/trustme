#![feature(inherent_associated_types, type_alias_impl_trait)]
#![allow(incomplete_features)]

struct Wrapper<T>(T);

impl Wrapper<i32> {
    type Item = u32;
}

type Hidden = impl Sized;

#[define_opaque(Hidden)]
fn check() {
    let _: Wrapper<Hidden>::Item = 42;
}

fn main() {
    check();
}
