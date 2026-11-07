//@ compile-flags: --target=$DIR/emulated_i128_target.toml -Zmir-opt-level=0
//@ input: emulated_i128_target.toml
#![feature(core_intrinsics)]
#![allow(internal_features)]

use std::{hint::black_box, intrinsics::float_to_int_unchecked};

fn main() {
    unsafe {
        assert_eq!(float_to_int_unchecked::<f32, i128>(black_box(-83.75)), -83);
        assert_eq!(float_to_int_unchecked::<f32, u128>(black_box(61.875)), 61);
        assert_eq!(float_to_int_unchecked::<f64, i128>(black_box(-0.75)), 0);
        assert_eq!(float_to_int_unchecked::<f64, u128>(black_box(0.75)), 0);
        assert_eq!(
            float_to_int_unchecked::<f64, i128>(black_box(-1267650600228229401496703205376.0)),
            -(1i128 << 100),
        );
        assert_eq!(
            float_to_int_unchecked::<f32, u128>(black_box(1208925819614629174706176.0)),
            1u128 << 80,
        );
    }
}
