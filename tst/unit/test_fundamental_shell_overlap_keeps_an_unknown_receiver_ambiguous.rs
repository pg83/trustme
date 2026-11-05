//@ aux-build: fundamental_overlap_shell.rs
//@ compile-flags: -Znext-solver
//@ compile-fail: error
extern crate fundamental_overlap_shell;

use fundamental_overlap_shell::{Remote, Shell};

trait Choose {}
struct Local;
impl<T> Choose for T where Shell<T>: Remote {}
impl Choose for Local {}

fn require<T: Choose>() {}

fn main() {
    require::<_>();
}
