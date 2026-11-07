#![allow(unused_imports)]

extern crate core as depot;

use right::mem::size_of_val as bytes;
use right::num::NonZeroU16 as Word;

mod left {
    pub use crate::right::*;
    pub use crate::depot::*;
}

mod right {
    pub use crate::left::*;
}

fn main() {
    let value = Word::new(709).unwrap();
    assert_eq!(value.get(), 709);
    assert_eq!(bytes(&value), 2);
}
