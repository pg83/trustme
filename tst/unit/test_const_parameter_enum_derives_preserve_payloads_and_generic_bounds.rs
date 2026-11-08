#![feature(adt_const_params, unsized_const_params)]
#![allow(incomplete_features)]

use std::marker::{ConstParamTy, UnsizedConstParamTy};

#[non_exhaustive]
#[derive(PartialEq, Eq, ConstParamTy)]
enum Code {
    Byte(u8),
    Flag { enabled: bool },
}

#[derive(PartialEq, Eq, std::marker::UnsizedConstParamTy)]
enum Packet<T> {
    Data { value: T },
    Empty,
}

fn code<const KEY: Code>() -> u16 {
    match KEY {
        Code::Byte(value) => value as u16,
        Code::Flag { enabled } => enabled as u16,
    }
}

fn checked<T: UnsizedConstParamTy>(value: T) -> T { value }

fn main() {
    assert_eq!(code::<{ Code::Byte(7) }>(), 7);
    assert_eq!(code::<{ Code::Flag { enabled: true } }>(), 1);
    let packet = checked(Packet::Data { value: 19u16 });
    assert!(matches!(packet, Packet::Data { value: 19 }));
    assert!(Packet::<u16>::Empty == Packet::<u16>::Empty);
}
