//@ compile-flags: --target=$DIR/emulated_i128_target.toml -Zmir-opt-level=0
//@ input: emulated_i128_target.toml
#![feature(core_intrinsics)]

#[inline(never)]
fn signed_bits(value: i128) -> (i128, i128) {
    unsafe {
        (
            core::intrinsics::bswap::<i128>(value),
            core::intrinsics::bitreverse::<i128>(value),
        )
    }
}

fn main() {
    let input = std::hint::black_box(0x80000000000000000000000000000003u128 as i128);
    let (bytes, bits) = signed_bits(input);
    assert!(bytes == 0x03000000000000000000000000000080i128);
    assert!(bits == 0xc0000000000000000000000000000001u128 as i128);
}
