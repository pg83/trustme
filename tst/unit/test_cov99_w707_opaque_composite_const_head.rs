//@ crate-type: lib
#![feature(type_alias_impl_trait, generic_const_exprs)]
#![allow(incomplete_features, dead_code)]

trait Witness {}
type Hidden = impl Copy;

fn require<T: Witness>() {}

#[define_opaque(Hidden)]
fn define<const N: usize>() -> Hidden
where
    (u8, [(); N + 1]): Witness,
    (Hidden, [(); N + 2]): Witness,
{
    require::<(Hidden, [(); N + 2])>();
    7u8
}
