#[derive(Debug, PartialEq)]
pub enum Signal { Idle, Value(u16), Named { code: u8 } }
pub use core::cfg as platform;
pub use core::stringify as spelling;
