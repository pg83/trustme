//@ compile-flags: -Coverflow-checks=off
fn values() -> (i128, u128, bool, f64) {
    let signed = const { (1i128 << 100) + 37 };
    let unsigned = const { (1u128 << 100) + 37 };
    let boolean = const { true };
    let real = const { 3.5f64 };
    (-signed, !unsigned, !boolean, -real)
}
fn main() {
    assert_eq!(values(), (-((1i128 << 100) + 37), u128::MAX ^ ((1u128 << 100) + 37), false, -3.5));
}
