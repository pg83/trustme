//@ crate-type: lib
//@ compile-flags: --emit=metadata

#![feature(adt_const_params)]
#![allow(incomplete_features)]

struct Bytes<const VALUE: [u8; 17]>;

trait Marker {}

impl Marker for Bytes<{ [1; 17] }> {}

fn require<T: Marker>() {}

pub fn exercise() {
    require::<Bytes<{ [1; 17] }>>();
}
