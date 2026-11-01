#![feature(trait_alias)]

pub trait Read<T> { fn read(&self, input: T) -> T; }
pub trait First<T> = Read<T>;
pub trait Second<T> = First<T> + Send;
