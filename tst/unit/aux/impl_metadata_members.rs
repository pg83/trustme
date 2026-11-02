pub trait Record {
    type Output;
    const ID: u32;
    fn value(&self) -> Self::Output;
}
pub trait Label { fn label(&self) -> u32; }
pub struct Leaf(pub u32);
impl Record for Leaf {
    type Output = u32;
    const ID: u32 = 17;
    fn value(&self) -> u32 { self.0 + Self::ID }
}
impl Label for Leaf { fn label(&self) -> u32 { self.0 + 3 } }
impl Leaf { pub const WIDTH: usize = 4; }
pub static MODULE_VALUE: u32 = 41;

pub fn consume<F: FnOnce(u32) -> u32>(callback: F) -> u32 { callback(5) }
pub fn repeat<F: FnMut(u32) -> u32>(mut callback: F) -> u32 { callback(2) + callback(3) }
pub fn shared<F: Fn(u32) -> u32>(callback: F) -> u32 { callback(7) }
pub async fn next(value: u32) -> u32 { Leaf(value).value() }
