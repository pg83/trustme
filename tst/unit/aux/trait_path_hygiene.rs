pub trait Compute<T> {
    type Result;
    const OFFSET: u32;
    fn calc(self, input: T) -> Self::Result;
}
impl Compute<u8> for u16 {
    type Result = u32;
    const OFFSET: u32 = 19;
    fn calc(self, input: u8) -> u32 { u32::from(self) * 10 + u32::from(input) + Self::OFFSET }
}
