#![feature(trivial_bounds)]
#![allow(incomplete_features)]
pub trait Pick { type Item; }
impl Pick for () { type Item = u16; }
pub trait Select {
    fn dormant() -> u16 where (): Pick<Item = u8> { 3 }
    fn value() -> u16 where (): Pick<Item = u16> { 47 }
    fn lifetime<'a>() -> u16 where 'a: 'a { 61 }
}
pub struct Live;
impl Select for Live where (): Pick<Item = u16> {}
pub struct Dormant;
impl Select for Dormant where (): Pick<Item = u8> {}
pub fn answer() -> u16 { <Live as Select>::value() + <Live as Select>::lifetime() }
