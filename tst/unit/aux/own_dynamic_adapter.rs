#![feature(no_core)]
#![no_core]
extern crate own_dynamic_base;
#[inline(never)]
pub fn through(value: u32) -> u32 { own_dynamic_base::keep(value) }
