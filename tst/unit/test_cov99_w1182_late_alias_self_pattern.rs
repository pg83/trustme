//@ edition: 2021
//@ crate-type: lib
//@ compile-flags: --emit=metadata
//@ aux-build: cov99_w1182_primitive_module.rs

use cov99_w1182_primitive_module::u8;

trait Inspect {
    fn inspect(self);
}

impl Inspect for Later {
    fn inspect(self) {
        match self {
            Self => (),
        }
    }
}

type Later = u8::Token;
