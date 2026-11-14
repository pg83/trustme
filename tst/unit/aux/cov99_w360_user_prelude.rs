#![feature(decl_macro)]

#[macro_export]
macro_rules! cov99_w360_value {
    () => { 360u16 };
}

pub mod definition {
    pub macro from_prelude() { cov99_w360_value!() }
}
