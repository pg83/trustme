//@ aux-build: reexported_use_targets.rs
#![feature(import_trait_associated_functions)]

mod facade { pub use reexported_use_targets::{Mode, Source}; }

use crate::facade::Source::{EMPTY, Item, make, read};
use crate::facade::Mode::Second;

struct Token(u16);
impl facade::Source for Token {
    type Item = u8;
    fn make() -> Self { Token(42) }
    fn read(&self) -> u16 { self.0 }
}

fn main() {
    let token: Token = make();
    let empty: Option<Token> = EMPTY;
    assert_eq!(read(&token), 42);
    assert!(empty.is_none());
    assert_eq!(Second as u8, 11);
}
