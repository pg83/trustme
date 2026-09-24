//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
// tokio's uds_datagram test counts datagrams down with `count -= 1` inside a
// `#[tokio::test]` body. Our side of the proc-macro bridge sent `-=` as `-`
// (and `<<=` / `>>=` as `<=` / `>=`), so the macro's output read
// `count - 1;` and the loop never ended.

use proc_macro_item_passthrough::echo_item;

#[echo_item]
fn compound(mut a: u32, mut b: u32, mut c: u32) -> (u32, u32, u32) {
    a -= 1;
    b <<= 2;
    c >>= 1;
    (a, b, c)
}

fn main() {
    assert_eq!(compound(5, 3, 8), (4, 12, 4));
}
