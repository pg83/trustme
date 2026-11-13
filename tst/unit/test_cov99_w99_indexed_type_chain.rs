//@ aux-build: cov99_w99_type_origin.rs
//@ aux-build: cov99_w99_type_bridge.rs

mod imported {
    pub use cov99_w99_type_bridge::indexed::*;
}

use imported::*;

fn main() {
    let record = Final { value: 7 };
    assert_eq!(record.value, 7);
}
