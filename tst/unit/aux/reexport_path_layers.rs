pub mod original {
    pub struct Number(pub u16);
    pub fn read(value: Number) -> u16 { value.0 }
    pub enum Choice { Left = 3, Right = 7 }
    pub mod leaf { pub const STEP: u16 = 11; }
}
pub mod first { pub use crate::original::{Number, read, Choice, leaf}; }
pub mod second { pub use crate::first::{Number as Value, read as extract, Choice as Branch, leaf as nested}; }
