//@ compile-fail: error:
#![feature(specialization)]
#![allow(incomplete_features)]

trait Rank {
    default fn rank(&self) -> u16 { 331 }
}

fn main() {}
