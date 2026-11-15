//@ compile-only
//@ aux-build: cov99_w960_remote_lang_items.rs
//@ compile-flags: -C codegen-type=monomir -Z mir-opt-level=0
#![feature(no_core, lang_items, intrinsics)]
#![no_core]

extern crate cov99_w960_remote_lang_items;
use cov99_w960_remote_lang_items::{Remote, Sized};

#[lang = "type_id"]
pub struct TypeId(u128);

#[rustc_intrinsic]
pub const fn type_id<T: ?Sized + 'static>() -> TypeId;

#[lang = "drop"]
pub trait Drop {
    fn drop(&mut self);
}

impl Drop for Remote {
    fn drop(&mut self) {
        let _ = type_id::<u16>();
    }
}

fn main() {
    let _remote = Remote;
}
