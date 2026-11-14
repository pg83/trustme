//@ compile-only
//@ compile-flags: -Z stop-after=typeck -Z dump-hir
#![feature(type_alias_impl_trait, deref_patterns)]

type Hidden = impl Copy;

#[define_opaque(Hidden)]
fn seed(deref!((_,)): Box<Hidden>) {
    let _: Hidden = (|| {},);
}

const SIZE: usize = std::mem::size_of::<Hidden>();

fn main() {}
