#[derive(Debug, PartialEq)]
pub enum Item<'a> {
    Literal(&'a [u8]),
    Component(u8),
    Compound(&'a [Item<'a>]),
}

pub mod nested {
    #[derive(Debug, PartialEq)]
    pub struct Pair(pub u8, pub u16);
}
