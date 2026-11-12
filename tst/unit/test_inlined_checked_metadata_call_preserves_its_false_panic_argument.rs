//@ input: support/metadata/own_checked_false_inline.metadata
//@ input: support/metadata/inline_checked_false_call.py
//@ compile-flags: --extern checked=$DIR/support/metadata/own_checked_false_inline.metadata -O

extern crate checked;

fn main() {
    let mut count = 0;
    checked::entry(&std::hint::black_box([17u16; 3]), &mut count);
    assert_eq!(count, 6);
    checked::entry(&std::hint::black_box([29u16; 3]), &mut count);
    assert_eq!(count, 12);

    checked::entry(&(), &mut count);
    assert_eq!(count, 12);
}
