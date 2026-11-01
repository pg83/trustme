//@ aux-build: reexported_use_targets.rs
#![feature(decl_macro, import_trait_associated_functions)]

mod facade { pub use reexported_use_targets::{Mode, Source}; }

struct Token(u16);
impl facade::Source for Token {
    type Item = u8;
    fn make() -> Self { Token(17) }
    fn read(&self) -> u16 { self.0 }
}

macro read_token($token:expr) {{
    use crate::facade::Source::{read, Item};
    use crate::facade::Mode::First;
    (read($token), First as u16)
}}

fn main() { assert_eq!(read_token!(&Token(39)), (39, 3)); }
