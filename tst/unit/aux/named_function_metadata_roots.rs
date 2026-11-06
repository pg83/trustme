pub struct Bias<const N: u8>;
impl<const N: u8> Bias<N> {
    pub fn add(value: u8) -> u8 { value + N }
}
pub fn add_free<const N: u8>(value: u8) -> u8 { value + N }
pub fn from_free<const N: u8>() -> impl Fn(u8) -> u8 { add_free::<N> }
pub fn from_inherent<const N: u8>() -> impl Fn(u8) -> u8 { Bias::<N>::add }

pub trait AddTag {
    fn add(value: u8) -> u8;
}
impl AddTag for Bias<11> {
    fn add(value: u8) -> u8 { value + 11 }
}
pub fn from_trait() -> impl Fn(u8) -> u8 { <Bias<11> as AddTag>::add }
