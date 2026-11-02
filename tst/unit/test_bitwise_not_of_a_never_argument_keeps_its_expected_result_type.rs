//@ compile-variant: -O
#![feature(never_type)]

fn invert(value: !) -> u32 { !value }
fn main() { let _: fn(!) -> u32 = invert; }
