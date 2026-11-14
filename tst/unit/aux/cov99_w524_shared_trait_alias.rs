#![feature(trait_alias)]

pub trait Leaf {
    fn number(&self) -> u32;
}

pub trait Shared = Leaf;
