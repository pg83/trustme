#![feature(adt_const_params, unsized_const_params)]
#![allow(incomplete_features)]

use std::marker::UnsizedConstParamTy;

trait Encoding {
    type Word;
}

impl Encoding for u8 {
    type Word = u16;
}

#[derive(PartialEq, Eq, std::marker::UnsizedConstParamTy)]
struct Signature<E: Encoding> {
    words: [E::Word; 2],
}

fn checked<K: UnsizedConstParamTy>(key: K) -> K {
    key
}

fn main() {
    let signature = checked(Signature::<u8> { words: [5, 41] });
    assert!(signature == Signature { words: [5, 41] });
    assert!(signature != Signature { words: [41, 5] });
}
