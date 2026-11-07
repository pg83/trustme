#![allow(unused_imports)]

extern crate core as depot;

mod left {
    pub use crate::right::*;
    pub use crate::depot::*;
}

mod right {
    pub use crate::left::*;
}

fn main() {
    let value = left::num::NonZeroU16::new(701).unwrap();
    assert_eq!(value.get(), 701);
    assert_eq!(right::mem::size_of_val(&value), 2);
}
