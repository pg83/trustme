//@ compile-variant: -O
use std::cell::Cell;
struct Named { value: Option<Cell<u16>>, tag: u8 }
struct Tuple(Option<Cell<u16>>);
const BASE: Named = Named { value: None, tag: 17 };
const UPDATED: &Named = &Named { tag: 23, ..BASE };
const FIELD: &Option<Cell<u16>> = &BASE.value;
const TUPLE: &Tuple = &Tuple(None);
const BLOCK: &Option<Cell<u16>> = &{ None };
const REPEATED: &[Option<Cell<u16>>; 1] = &[None; 1];
const LIST: &[Option<Cell<u16>>; 2] = &[None, None];
fn main() {
    assert_eq!(UPDATED.tag, 23);
    assert!(UPDATED.value.is_none());
    assert!(FIELD.is_none());
    assert!(TUPLE.0.is_none());
    assert!(BLOCK.is_none());
    assert!(REPEATED[0].is_none());
    assert!(LIST[0].is_none() && LIST[1].is_none());
}
