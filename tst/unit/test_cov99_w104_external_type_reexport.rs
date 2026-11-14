//@ aux-build: cov99_w104_type_reexport.rs
//@ crate-type: lib

use cov99_w104_type_reexport::Exposed;

pub fn value(item: Exposed) -> u8 {
    item.value
}
