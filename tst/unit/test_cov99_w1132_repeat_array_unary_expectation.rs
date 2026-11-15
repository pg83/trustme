//@ compile-fail: Failed to find an impl of
//@ compile-flags: --emit=metadata -Cpanic=abort

fn main() {
    let _: [u8; 2] = ![1u8; 2];
}
