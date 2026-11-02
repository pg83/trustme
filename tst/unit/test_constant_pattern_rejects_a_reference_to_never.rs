//@ compile-fail: uninhabited value in constant pattern
#![feature(never_type)]
const INVALID: &! = unsafe { core::mem::transmute(1usize) };
fn classify(value: &!) -> bool { matches!(value, INVALID) }
fn main() { let _ = classify as fn(&!) -> bool; }
