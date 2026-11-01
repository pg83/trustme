#![feature(trait_alias)]

pub trait Read<T> {
    type Item;
    fn read(&self, value: T) -> Self::Item;
}

pub trait First<T> = Read<T> + Send;
pub trait Second<T> = First<T> + Sync;
pub trait Markers = Send + Sync;
