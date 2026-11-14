//@ proc-macro-aux-build: cov99_w210_vtable_ident.rs
//@ aux-build: cov99_w210_vtable_seed.rs
//@ env: TRUSTME_DUMP_PROCMACRO=$OUT-procmacro

use cov99_w210_vtable_ident::owned_vtable;

struct Anchor;
impl Anchor {
    fn w210_probe(&self) {}
}

trait Probe {
    fn w210_probe(self);
}
impl<T> Probe for T {
    fn w210_probe(self) {}
}

fn main() {
    let _ = owned_vtable!();
}
