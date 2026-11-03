#![feature(trait_alias, extern_types)]
pub extern crate core as foundation;
pub trait View = core::fmt::Debug + Copy;
extern "C" { pub type Foreign; }
pub mod nested {
    pub use crate::foundation as library;
    pub use crate::Foreign as Opaque;
    pub use crate::View as Printable;
}
