pub trait Evaluate { fn evaluate(&self) -> u16; }
pub struct Record<const N: usize>(pub [u8; N]);
pub const OFFSET: u16 = 7;
pub fn evaluate<T: Evaluate>(value: &T) -> u16 { value.evaluate() + OFFSET }
