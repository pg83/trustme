//@ aux-build: fundamental_overlap_shell.rs
//@ compile-flags: -Znext-solver
//@ compile-fail: error
extern crate fundamental_overlap_shell;

use fundamental_overlap_shell::{Remote, Shell};

trait Produce {
    type Item;
}

struct Package<T>(T);

trait Pick {
    fn pick(&self) -> u16;
}

impl<T: Produce> Pick for Package<T> where Shell<T::Item>: Remote {
    fn pick(&self) -> u16 { 617 }
}

impl<T: Produce> Pick for Package<T> {
    fn pick(&self) -> u16 { 619 }
}

fn dispatch<T: Produce>(value: &Package<T>) -> u16 {
    value.pick()
}

fn main() {}
