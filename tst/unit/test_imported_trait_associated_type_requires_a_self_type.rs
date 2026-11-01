//@ aux-build: imported_trait_parameters.rs
//@ compile-fail: not allowed in function signatures
extern crate imported_trait_parameters;

mod facade {
    pub use imported_trait_parameters::Convert;
}

fn ambiguous(_: crate::facade::Convert<u8>::Output) {}

fn main() {}
