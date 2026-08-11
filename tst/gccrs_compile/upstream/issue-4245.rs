
#![cfg_attr(not(cg_gcc), feature(intrinsics))]
#![feature(lang_items)]

#[cfg(not(cg_gcc))]
extern "rust-intrinsic" {
    fn copy_nonoverlapping<T>(src: *const T, dst: *mut T, count: usize);
}
