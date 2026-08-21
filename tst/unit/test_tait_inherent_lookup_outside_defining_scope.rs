//@ crate-type: lib
//@ compile-fail: Type mismatch

#![feature(type_alias_impl_trait)]

struct Wrapper<T>(T);

impl Wrapper<u32> {
    fn target() {}
}

type Hidden = impl Sized;

#[define_opaque(Hidden)]
fn hidden() -> Hidden {
    0_u32
}

fn outside() {
    Wrapper::<Hidden>::target();
}
