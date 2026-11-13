#![feature(inherent_associated_types)]
#![allow(incomplete_features)]

pub struct Token;

impl Token {
    pub type Item = u32;
}

pub fn pair() -> (impl Copy, Token::Item) {
    (true, 96u32)
}
