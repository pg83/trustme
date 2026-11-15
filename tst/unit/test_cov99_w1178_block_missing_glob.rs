//@ compile-fail: Unable to find cov99_w1178_missing
//@ compile-flags: --emit=metadata -Cpanic=abort

fn main() {
    use cov99_w1178_missing::*;
}
