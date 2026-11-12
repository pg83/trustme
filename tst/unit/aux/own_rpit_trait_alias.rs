#![feature(trait_alias)]
#![no_std]

pub trait Marker {}

pub trait Source {
    fn iter() -> impl Iterator<Item = u16>;
}

pub trait Sources = Marker + Source;
