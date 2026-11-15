pub trait Shape {
    type Item;
}

pub struct Byte;

impl Shape for Byte {
    type Item = u8;
}

pub const MARK: <Byte as Shape>::Item = 7;
pub const PAIR: (<Byte as Shape>::Item, ()) = (7, ());
