//@ crate-type: lib
#![feature(type_alias_impl_trait, generic_const_exprs)]
#![allow(incomplete_features, dead_code)]

trait Witness<A> {}
type Hidden = impl Copy;

trait Carrier<const N: usize>
where
    [(); N + 1]:,
    [(); N + 2]:,
{
    type Item: Witness<(u8, [(); N + 1])> + Witness<(Hidden, [(); N + 2])>;
}

fn require<T: Witness<A>, A>() {}

#[define_opaque(Hidden)]
fn define<C: Carrier<N>, const N: usize>() -> Hidden
where
    [(); N + 1]:,
    [(); N + 2]:,
{
    require::<C::Item, (Hidden, [(); N + 2])>();
    7u8
}
