//@ aux-build: fundamental_overlap_shell.rs
//@ compile-flags: -Znext-solver
//@ compile-fail: error
extern crate fundamental_overlap_shell;

use fundamental_overlap_shell::{Remote, Shell};

trait Produce {
    type Item;
}

struct Relay<T>(T);

impl<T: Produce> Produce for Relay<T> {
    type Item = T::Item;
}

trait Pick<T> {
    type Output;
    fn pick(&self) -> Self::Output;
}

impl<T: Produce> Pick<T> for u8 where Shell<T::Item>: Remote {
    type Output = u16;
    fn pick(&self) -> u16 { 631 }
}

impl<T> Pick<Relay<T>> for u8 {
    type Output = u32;
    fn pick(&self) -> u32 { 641 }
}

fn main() {
    let _value = 7u8.pick();
}
