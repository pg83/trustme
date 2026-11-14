//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(sized_hierarchy)]

pub trait SliceView
where
    Self: core::marker::PointeeSized,
{
}

impl SliceView for [u8] {}
