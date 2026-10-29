#![feature(pattern_types, pattern_type_macro)]
use std::pat::pattern_type;
pub type Bounded = pattern_type!(u32 is 2..=6);
pub type Below = pattern_type!(u32 is ..4);
pub type Above = pattern_type!(u32 is 9..);
#[inline(never)]
pub fn bounded(value: Bounded) -> u32 { unsafe { std::mem::transmute(value) } }
#[inline(never)]
pub fn below(value: Below) -> u32 { unsafe { std::mem::transmute(value) } }
#[inline(never)]
pub fn above(value: Above) -> u32 { unsafe { std::mem::transmute(value) } }
