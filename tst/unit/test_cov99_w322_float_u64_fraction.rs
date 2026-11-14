//@ compile-flags: -Copt-level=2

fn main() {
    let value = 0.5_f64;
    assert_eq!(value as u64, 0);
}
