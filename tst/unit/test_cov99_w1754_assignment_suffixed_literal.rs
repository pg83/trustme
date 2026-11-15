//@ compile-fail: invalid left-hand side of assignment
//@ compile-flags: --emit=metadata

fn main() {
    1w1754 = 0;
}
