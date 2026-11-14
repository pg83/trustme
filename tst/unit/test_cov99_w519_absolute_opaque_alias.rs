//@ edition: 2021
//@ compile-flags: -Cpanic=abort
#![feature(type_alias_impl_trait)]

mod factory {
    pub type Hidden = impl PartialEq<u8>;

    #[define_opaque(crate::factory::Hidden)]
    pub fn make() -> Hidden {
        37u8
    }
}

fn main() {
    assert!(factory::make() == 37u8);
}
