//@ aux-build: fundamental_overlap_shell.rs
//@ compile-flags: -Znext-solver
//@ compile-fail: error
extern crate fundamental_overlap_shell;

use fundamental_overlap_shell::{Remote, Shell};

trait Choose {
    type Output;
    fn chosen() -> Self::Output;
}

struct Local;
impl<T> Choose for T where Shell<T>: Remote {
    type Output = u16;
    fn chosen() -> u16 { 521 }
}
impl Choose for Local {
    type Output = u32;
    fn chosen() -> u32 { 523 }
}

fn pick<T: Choose>() -> T::Output { T::chosen() }

fn main() {
    let _value = pick::<_>();
}
