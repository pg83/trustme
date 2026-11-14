//@ proc-macro-aux-build: cov99_w221_vtable_type.rs
//@ aux-build: cov99_w221_move_seed.rs
//@ aux-build: cov99_w221_owned_export.rs
//@ compile-flags: -Z dump-hir
//@ env: TRUSTME_DUMP_PROCMACRO=$OUT-procmacro

use cov99_w221_owned_export::{Exported, FirstArg};

impl () {
    fn w221_install(self: <Exported as FirstArg>::Arg) {}
}

fn main() {
    assert_eq!(std::mem::size_of::<<Exported as FirstArg>::Arg>(), std::mem::size_of::<&()>());
}
