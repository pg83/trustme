//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

fn main() {
    let array = Box::new([1u8, 2u8]);
    let slice: Box<[u8]> = Box::new([3u8, 4u8]);
    let _values = [array, slice];
}
