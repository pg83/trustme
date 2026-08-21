#![feature(adt_const_params, unsized_const_params)]
#![feature(negative_impls)]
#![allow(incomplete_features)]

trait Marker<const VALUE: &'static str> {}

struct Wrapper;

impl !Marker<NAMED> for Wrapper {}

const NAMED: &str = "";

fn main() {}
