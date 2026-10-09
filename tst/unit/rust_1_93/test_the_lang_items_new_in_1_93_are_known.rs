//@ crate-type: lib
// core 1.93 defines lang items rustc 1.92 does not know: the `offset_of`
// intrinsic, the profiling markers `compiler_move` and `compiler_copy`,
// `into_try_type`, `SizedTypeProperties::{SIZE, ALIGN}` (`mem_size_const`,
// `mem_align_const`) and `TrivialClone`. We stopped with "Unknown language
// item".
#![feature(no_core, lang_items, intrinsics, rustc_attrs)]
#![allow(internal_features)]
#![no_core]

#[lang = "pointee_sized"]
pub trait PointeeSized {}

#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}

#[lang = "sized"]
pub trait Sized: MetaSized {}

#[lang = "copy"]
pub trait Copy: Clone {}

#[lang = "clone"]
pub trait Clone: Sized {}

#[rustc_intrinsic]
#[lang = "offset_of"]
pub const fn offset_of<T: PointeeSized>(variant: u32, field: u32) -> usize;

#[rustc_intrinsic]
pub const fn size_of<T>() -> usize;

#[rustc_intrinsic]
pub const fn align_of<T>() -> usize;

#[lang = "compiler_move"]
pub fn compiler_move<T, const SIZE: usize>(_src: *const T, _dst: *mut T) {}

#[lang = "compiler_copy"]
pub fn compiler_copy<T, const SIZE: usize>(_src: *const T, _dst: *mut T) {}

pub trait SizedTypeProperties: Sized {
    #[lang = "mem_size_const"]
    const SIZE: usize = size_of::<Self>();

    #[lang = "mem_align_const"]
    const ALIGN: usize = align_of::<Self>();
}

#[lang = "trivial_clone"]
pub unsafe trait TrivialClone: Clone {}

#[lang = "into_try_type"]
pub fn residual_into_try_type<R>(r: R) -> R {
    r
}
