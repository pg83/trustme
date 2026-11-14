//@ crate-type: lib
//@ compile-only
//@ compile-flags: --emit=metadata
#![feature(adt_const_params, unsized_const_params)]

static NAMED: [u8; 1] = [1];

struct Key<const DATA: &'static [u8; 1]>;
trait Selected {}

impl Selected for Key<{ &NAMED }> {}
impl Selected for Key<{ b"\x02" }> {}

fn require<T: Selected>() {}

pub fn select() {
    require::<Key<{ b"\x02" }>>();
}
