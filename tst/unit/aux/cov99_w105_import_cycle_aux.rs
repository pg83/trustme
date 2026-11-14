//@ edition: 2021

pub mod left {
    pub use crate::right::*;

    pub fn seed() -> u32 { 105 }

    pub enum Kind { Only }
    pub use Kind::Only as Variant;
}

pub mod right {
    pub use crate::left::*;
}

pub use right::{seed as Exported, Kind, Variant};
