//@ compile-only
#![feature(specialization)]

trait Source { type Item; }
trait Select<T> { fn tag() -> u8; }

struct Choice;
struct Concrete;

impl Source for Concrete { type Item = u16; }

impl<T: Source> Select<T> for Choice
where
    T::Item: std::fmt::Display,
{
    default fn tag() -> u8 { 1 }
}

impl Select<Concrete> for Choice {
    fn tag() -> u8 { 2 }
}

fn main() {
    let _ = <Choice as Select<_>>::tag();
}
