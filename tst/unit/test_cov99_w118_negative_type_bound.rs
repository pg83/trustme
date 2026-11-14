#![feature(negative_bounds)]
#![allow(incomplete_features, dead_code)]

trait Forbidden {}

fn accepts_negative<T: !Forbidden>() {}

fn main() {}
