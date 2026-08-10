// Extracted from library/core/src/mem/mod.rs:161
#![allow(unused)]
#![feature(unsized_fn_params, forget_unsized)]
#![allow(internal_features)]
fn main() {

    use std::mem::forget_unsized;

    pub fn in_place() {
        forget_unsized(*Box::<str>::from("str"));
    }

    pub fn param(x: str) {
        forget_unsized(x);
    }
}
