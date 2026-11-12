//@ input: support/metadata/own_shared_cleanup_termination.metadata
//@ input: support/metadata/terminate_cleanup_paths.py
//@ compile-flags: --extern boundary=$DIR/support/metadata/own_shared_cleanup_termination.metadata -O

extern crate boundary;

fn main() {
    let mut count = 0;
    let value = 17u64;
    for first in [std::hint::black_box(false), std::hint::black_box(true)] {
        boundary::guarded(&value, &mut count, first);
    }
    assert_eq!(count, 16);

    let empty = [0u8; 0];
    for first in [std::hint::black_box(false), std::hint::black_box(true)] {
        boundary::guarded(&empty, &mut count, first);
    }
    assert_eq!(count, 16);
}
