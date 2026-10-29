#[derive(Clone, Copy)] pub struct End;
pub const LIMIT: u8 = 5;
pub use End as Stop;
pub const OUTER: u8 = 8;
pub use OUTER as EDGE;
pub fn helper() -> u8 { 90 }
#[derive(Clone, Copy)] pub enum Tag { Unit, Tuple(u8), Named { value: u8 } }
#[derive(Clone, Copy)] pub enum Bit { Off = 2, On = 7 }
