#[derive(Debug, PartialEq)]
pub struct Pair(pub u16, pub u16);
#[derive(Debug, PartialEq)]
pub enum Choice { Pair(u16, u16) }
