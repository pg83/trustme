#![feature(trait_alias)]
pub trait Word<T> { type Item; fn word() -> Self::Item; }
pub trait Marker {}
pub trait Parent<T>: Word<T> {}
pub trait Readable<T = u8> = Parent<T> + Marker;
pub trait Portable<T = u8> = Send + Readable<T>;
pub struct Input;
impl Marker for Input {}
impl Word<u8> for Input { type Item = u16; fn word() -> u16 { 513 } }
impl Word<u16> for Input { type Item = u32; fn word() -> u32 { 65539 } }
impl Parent<u8> for Input {}
impl Parent<u16> for Input {}
