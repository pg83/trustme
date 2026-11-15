//@ compile-fail: Invalid cast [fcn src]
//@ compile-flags: --emit=metadata -Cpanic=abort

fn main() {
    let _ = async {} as fn();
}
