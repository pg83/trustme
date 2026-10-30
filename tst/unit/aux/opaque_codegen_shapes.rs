#[inline(never)]
pub fn sequence<const N: usize>(value: u8) -> impl Iterator<Item = u8> + Clone { [value; N].into_iter() }
pub struct Maker(pub u8);
impl Maker {
    #[inline(never)]
    pub fn sequence<const N: usize>(&self) -> impl Iterator<Item = u8> + Clone { [self.0; N].into_iter() }
}
pub trait DefaultSequence {
    fn defaults(value: u8) -> impl Iterator<Item = u8> + Clone { [value, value + 1].into_iter() }
}
impl DefaultSequence for Maker {}
pub async fn asynchronous(value: u8) -> u16 { u16::from(value) + 1 }
