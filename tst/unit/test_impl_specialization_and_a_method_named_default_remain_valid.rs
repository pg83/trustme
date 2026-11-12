#![feature(specialization)]
#![allow(incomplete_features)]

trait Rank {
    fn rank(&self) -> u16;
    fn default() -> u16 { 337 }
}

struct Record<T>(T);

impl<T> Rank for Record<T> {
    default fn rank(&self) -> u16 { 317 }
}

impl Rank for Record<u16> {
    fn rank(&self) -> u16 { 331 }
}

fn main() {
    assert_eq!(Record(1u16).rank(), 331);
    assert_eq!(Record(false).rank(), 317);
    assert_eq!(<Record<u16> as Rank>::default(), 337);
}
