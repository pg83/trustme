//@ edition: 2015
#![feature(trait_alias)]

pub trait Answer {
    fn answer(&self) -> u8;
}

impl Answer for u8 {
    fn answer(&self) -> u8 {
        *self
    }
}

pub trait Alias = Answer;

#[macro_export]
macro_rules! alias_answer {
    ($value:expr) => {
        $crate::Alias::answer(&$value)
    };
}
