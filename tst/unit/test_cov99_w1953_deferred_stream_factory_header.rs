//@ edition: 2024
#![feature(gen_blocks, inherent_associated_types)]
#![allow(incomplete_features)]

const fn extent() -> usize {
    let factory = || async gen {
        yield 31u8;
    };
    core::mem::forget(factory);
    7
}

struct Strip<T>(T);

impl Strip<[u8; 7]> {
    type Scalar = u64;
}

type Word = Strip<[u8; extent()]>::Scalar;

fn main() {
    let sample: Word = 127u64;
    assert_eq!(sample, 127);
    assert_eq!(core::mem::size_of::<Word>(), 8);
}
