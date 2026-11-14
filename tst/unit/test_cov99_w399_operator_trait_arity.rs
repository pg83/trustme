//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(no_core, lang_items)]
#![no_core]
#![allow(internal_features)]

#[lang = "sized"]
pub trait Sized {}

#[lang = "not"]
pub trait Not<Rhs = Self> {
    type Output;
    fn not(self) -> Self::Output;
}

impl Not for i32 {
    type Output = i32;
    fn not(self) -> i32 { 0 }
}

pub trait HasOutput {
    type Output;
}

#[lang = "add"]
pub trait Add<Rhs = Self>: HasOutput {
    fn add(self, rhs: Rhs) -> <Self as HasOutput>::Output;
}

impl HasOutput for i32 {
    type Output = i32;
}

impl Add for i32 {
    fn add(self, _rhs: i32) -> i32 { 0 }
}

pub fn unary() -> i32 { !1i32 }
pub fn binary() -> i32 { 1i32 + 2i32 }
