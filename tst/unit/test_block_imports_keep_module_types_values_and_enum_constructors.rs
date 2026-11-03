//@ compile-variant: -O
mod readings {
    pub struct Meter(pub u16);
    pub enum Sample { Empty, Reading(u16), Record { value: u16 } }
    pub union Packet { pub word: u16, pub bytes: [u8; 2] }
    pub trait Read { fn read(&self) -> u16; }
    impl Read for Meter { fn read(&self) -> u16 { self.0 } }
    pub type Number = u16;
    pub const OFFSET: u16 = 3;
    pub fn advance(value: u16) -> u16 { value + OFFSET }
}
mod bridge { pub use crate::readings::*; }
fn sample<T, const N: usize>() -> [u16; N] {
    use crate::bridge::{Meter as Scale, Number, Packet, Read, Sample};
    use crate::bridge::Sample::{Empty, Reading as Point, Record};
    use crate::bridge::{OFFSET as SHIFT, advance as next};
    use std::cmp::Ordering::Less;
    mod local { pub const BASE: u16 = 677; }
    use local::BASE;
    let value: Number = next(Scale(BASE).read());
    let packet = Packet { word: value };
    let pair = Packet { bytes: [7, 9] };
    assert_eq!(unsafe { packet.word }, BASE + SHIFT);
    assert_eq!(unsafe { pair.bytes }, [7, 9]);
    assert_eq!(value.cmp(&(value + 1)), Less);
    let mut sum = 0;
    for item in [Empty, Point(value), Record { value: 11 }] {
        sum += match item { Sample::Empty => 0, Sample::Reading(v) => v, Sample::Record { value } => value };
    }
    [sum; N]
}
fn main() { assert_eq!(sample::<u8, 2>(), [691, 691]); }
