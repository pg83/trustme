//@ compile-fail: error
fn contains<const N: usize>(value: usize) -> bool {
    match value {
        N..=N => true,
        _ => false,
    }
}
fn main() { let _ = contains::<4>(4); }
