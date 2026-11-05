#![feature(rustc_attrs)]
#![allow(internal_features)]
#[macro_export]
#[rustc_builtin_macro]
macro_rules! cfg { ($($tokens:tt)*) => {}; }

pub use crate::cfg as feature_gate;
pub mod gates {
    pub use crate::cfg as nested_gate;
}
