#![feature(decl_macro)]

pub mod arithmetic {
    pub macro adjusted($value:expr) {
        $value + 29u16
    }
}

pub mod exports {
    pub use crate::arithmetic::adjusted;
}
