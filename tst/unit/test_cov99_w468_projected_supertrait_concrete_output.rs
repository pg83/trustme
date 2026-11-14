//@ crate-type: lib
//@ compile-only
//@ compile-flags: -Z stop-after=hir -Z dump-hir

pub trait Mark {}

pub trait Projection {
    type Output;
}

pub trait Base {
    type Item;
    type Tag;
}

pub trait Projected: Projection + Base<Item = <Self as Projection>::Output> {}

pub trait Joined: Base<Item = u8, Tag: Mark> + Projected + Projection<Output = u8> {}
