//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

fn main() {
    let array = [1u8, 2u8];
    let slice: &[u8] = &array;
    let _values = [&array, slice];
}
