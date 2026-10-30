//@ aux-build: trait_glob_value_source.rs
#![feature(import_trait_associated_functions)]
mod bridge { pub use trait_glob_value_source::Source::*; }
use bridge::{EMPTY, create, read};
struct Token(u16);
impl trait_glob_value_source::Source for Token {
    type Item = u8;
    fn create() -> Self { Token(29) }
    fn read(self) -> u16 { self.0 }
}
fn main() {
    let value: Token = create();
    assert_eq!(read(value), 29);
    let empty: Option<Token> = EMPTY;
    assert!(empty.is_none());
}
