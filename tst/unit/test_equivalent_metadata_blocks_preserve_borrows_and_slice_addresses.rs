//@ input: support/metadata/own_equivalent_mir_blocks.metadata
//@ input: support/metadata/equivalent_mir_blocks.py
//@ compile-flags: --extern codec=$DIR/support/metadata/own_equivalent_mir_blocks.metadata -O

extern crate codec;

fn main() {
    let values = [17u16, 29, 43];
    let mut count = 0usize;
    for flag in [std::hint::black_box(false), std::hint::black_box(true)] {
        codec::borrowed(&values[1], &mut count, flag);
        assert_eq!(codec::data(values.as_slice() as *const [u16], flag), values.as_ptr());
    }
    assert_eq!(count, 4);

    let empty: [u8; 0] = [];
    for flag in [std::hint::black_box(false), std::hint::black_box(true)] {
        codec::borrowed(&empty, &mut count, flag);
        assert_eq!(codec::data(empty.as_slice() as *const [u8], flag), empty.as_ptr());
    }
    assert_eq!(count, 4);
}
