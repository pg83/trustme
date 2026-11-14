#![feature(specialization)]
#![allow(incomplete_features)]

trait Source { type Item; }
trait Family<T> { type Value; }
trait Choice { fn code() -> u8; }

struct Middle;
impl Source for Middle { type Item = u8; }

impl<T, U, V> Choice for T
where
    U: Source<Item = V>,
    T: Family<U, Value = V> + Source<Item = U>,
{
    default fn code() -> u8 { 1 }
}
impl<T> Choice for T
where
    T: Family<Middle, Value = u8> + Source<Item = Middle>,
{
    fn code() -> u8 { 2 }
}

struct Root;
impl Source for Root { type Item = Middle; }
impl Family<Middle> for Root { type Value = u8; }

fn main() { assert_eq!(Root::code(), 2); }
