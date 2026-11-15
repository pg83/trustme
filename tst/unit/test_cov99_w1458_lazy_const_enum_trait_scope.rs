//@ crate-type: lib
//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

#![no_std]

mod scope {
    enum Count {
        Two = 2,
    }
    type CountAlias = Count;

    struct Factory;
    impl Factory {
        const fn size() -> usize {
            CountAlias::Two as usize
        }
    }

    struct Key<const N: usize>;
    trait Select {
        type Out;
    }
    impl Select for Key<2> {
        type Out = u8;
    }

    pub fn check() {
        let _: <Key<{ Factory::size() }> as Select>::Out = 0u8;
    }
}

pub fn check() {
    scope::check();
}
