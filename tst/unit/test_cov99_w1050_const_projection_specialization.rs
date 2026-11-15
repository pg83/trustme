//@ crate-type: lib
//@ compile-flags: --emit=metadata
#![feature(specialization)]
#![allow(incomplete_features)]

trait HasArray {
    type Array;
}

trait Guard {
    type Checked;
}

impl Guard for [u8; 3] {
    type Checked = ();
}

struct Input;

impl HasArray for Input {
    type Array = [u8; 3];
}

trait Pick {
    fn pick() -> u8;
}

impl<T, const N: usize> Pick for T
where
    [u8; N]: Guard<Checked = ()>,
    T: HasArray<Array = [u8; N]>,
{
    default fn pick() -> u8 { 1 }
}

impl Pick for Input {
    fn pick() -> u8 { 2 }
}

pub fn selected() -> u8 {
    <Input as Pick>::pick()
}
