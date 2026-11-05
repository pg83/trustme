//@ aux-build: fundamental_overlap_shell.rs
//@ compile-flags: -Znext-solver
//@ compile-fail: error
extern crate fundamental_overlap_shell;

use fundamental_overlap_shell::{Remote, Shell};

trait Choose<T> {
    type Output;
    fn chosen(&self) -> Self::Output;
}

struct Local;
impl<T> Choose<T> for u8 where Shell<T>: Remote {
    type Output = u16;
    fn chosen(&self) -> u16 { 541 }
}
impl Choose<Local> for u8 {
    type Output = u32;
    fn chosen(&self) -> u32 { 547 }
}

fn main() {
    let _value = 3u8.chosen();
}
