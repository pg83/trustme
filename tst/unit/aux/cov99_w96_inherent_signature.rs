#![feature(inherent_associated_types)]
#![allow(incomplete_features)]

pub struct Token;

impl Token {
    pub type Item = u32;
}

pub fn keep(value: Token::Item) -> Token::Item {
    value
}
