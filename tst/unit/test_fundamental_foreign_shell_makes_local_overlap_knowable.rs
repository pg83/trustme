//@ aux-build: fundamental_overlap_shell.rs
//@ compile-flags: -Znext-solver
extern crate fundamental_overlap_shell;

use fundamental_overlap_shell::{Remote, Shell};

trait Choose {
    fn chosen(&self) -> u16;
}

struct Local(u16);

impl<T> Choose for T where Shell<T>: Remote {
    fn chosen(&self) -> u16 { 509 }
}

impl Choose for Local {
    fn chosen(&self) -> u16 { self.0 }
}

fn main() {
    assert_eq!(Local(503).chosen(), 503);
    assert_eq!(17u16.chosen(), 509);
}
