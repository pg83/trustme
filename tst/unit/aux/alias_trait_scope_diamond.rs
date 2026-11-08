#![feature(trait_alias)]

pub trait Read {
    fn read(&self) -> u16;
}

pub trait Shared = Read;
pub trait Left = Shared;
pub trait Right = Shared;
pub trait Diamond = Left + Right;
