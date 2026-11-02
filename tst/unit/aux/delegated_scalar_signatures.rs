pub struct Token<const B: bool, T, const C: char, const I: i16, const W: u128>(pub T);
pub enum Choice<const N: usize> { Bytes([u8; N]), Empty }
pub union Bits<const N: usize> { pub bytes: [u8; N], pub word: u64 }
pub trait Flagged<const B: bool> { fn flag(&self) -> bool { B } }
impl Flagged<true> for u8 {}
pub trait Scalars {
    fn map(&self, value: Token<true, u8, 'λ', -7, { 1u128 << 100 }>) -> Token<false, u16, '🦀', -9, { 1u128 << 110 }> { Token(value.0 as u16 + 3) }
    fn choice(&self, value: Choice<2>) -> u8 { match value { Choice::Bytes(bytes) => bytes[0] + bytes[1], Choice::Empty => 0 } }
    fn bits(&self, value: Bits<8>) -> u64 { u64::from_le_bytes(unsafe { value.bytes }) }
    fn flag<T: Flagged<true>>(&self, value: T) -> bool { value.flag() }
}
pub struct Source;
impl Scalars for Source {}
