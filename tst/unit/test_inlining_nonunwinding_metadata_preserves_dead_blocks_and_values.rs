//@ input: support/metadata/own_shared_cleanup_termination.metadata
//@ input: support/metadata/own_nonunwinding_dead_blocks.metadata
//@ input: support/metadata/inline_nonunwinding_dead_blocks.py
//@ compile-flags: --extern boundary=$DIR/support/metadata/own_nonunwinding_dead_blocks.metadata -O

extern crate boundary;

fn main() {
    let mut count = 0;
    for first in [std::hint::black_box(false), std::hint::black_box(true)] {
        boundary::guarded(&[17u16; 3], &mut count, first);
    }
    assert_eq!(count, 12);

    for first in [std::hint::black_box(false), std::hint::black_box(true)] {
        boundary::guarded(&(), &mut count, first);
    }
    assert_eq!(count, 12);
}
