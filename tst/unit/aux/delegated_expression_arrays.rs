#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

pub trait Windows {
    fn unchanged<const N: usize>(&self, input: [u16; N + 1]) -> [u16; N + 1]
    where [(); N + 1]:;
}
pub struct Source;
impl Windows for Source {
    fn unchanged<const N: usize>(&self, input: [u16; N + 1]) -> [u16; N + 1]
    where [(); N + 1]: { input }
}
