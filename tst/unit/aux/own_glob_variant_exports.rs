pub extern crate core as api_core;
pub enum Shape { Named { value: u32 }, Empty }
pub mod exported { pub use super::Shape::Named; }
