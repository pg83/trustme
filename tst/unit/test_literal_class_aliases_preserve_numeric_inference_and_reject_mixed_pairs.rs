//@ reject-flags: --edition=2021 --cfg mixed => mismatch || mismatched types
fn first<T>(left: T, _: T) -> T { left }
fn linked<T>(value: T) -> (T, T) where T: Copy { (value, value) }
fn main() {
    #[cfg(mixed)] let _ = first(3, 4.0);
    let integer = first(3, 9);
    let floating = first(1.25, 2.5);
    let (a, b) = linked(integer);
    assert_eq!(a + b, 6);
    assert_eq!(floating, 1.25);
    let exact: u16 = first(first(11, 17), 23);
    assert_eq!(exact, 11);
}
