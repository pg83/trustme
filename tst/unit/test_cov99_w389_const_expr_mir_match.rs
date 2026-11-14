//@ crate-type: lib
//@ compile-flags: --emit=metadata
//@ aux-build: cov99_w389_const_expr_mir.rs

use cov99_w389_const_expr_mir::{Number, Step};

fn require<T: Step<1>>() {}

pub fn exercise() {
    require::<Number<2>>();
}
