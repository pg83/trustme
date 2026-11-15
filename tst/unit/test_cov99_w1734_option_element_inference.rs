//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

fn main() {
    let _values = [None, Some(7u8)];
}
