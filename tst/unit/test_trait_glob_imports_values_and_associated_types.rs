#![feature(import_trait_associated_functions)]
mod definitions {
    pub trait Source: Sized {
        type Item;
        const EMPTY: Option<Self> = None;
        fn create() -> Self;
        fn read(self) -> u16;
    }
}
mod bridge { pub use super::definitions::Source::*; }
use bridge::{EMPTY, create, read};
struct Token(u16);
impl definitions::Source for Token {
    type Item = u8;
    fn create() -> Self { Token(17) }
    fn read(self) -> u16 { self.0 }
}
fn main() {
    let value: Token = create();
    assert_eq!(read(value), 17);
    let empty: Option<Token> = EMPTY;
    assert!(empty.is_none());
}
