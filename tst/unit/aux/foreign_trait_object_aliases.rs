#![feature(trait_alias)]

pub trait Bytes = Iterator<Item = u8> + Send;
pub trait Show = core::fmt::Debug + Send;
