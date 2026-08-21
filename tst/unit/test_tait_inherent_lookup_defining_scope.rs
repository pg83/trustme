//@ crate-type: lib

#![feature(type_alias_impl_trait)]

struct Wrapper<T>(T);

impl Wrapper<u32> {
    fn target() {}
}

type Hidden = impl Sized;

impl Wrapper<Hidden> {
    #[define_opaque(Hidden)]
    fn call() -> Hidden {
        Self::target();
        Wrapper::<Hidden>::target();
        let target: fn() = Self::target;
        target();
        0_u32
    }
}
