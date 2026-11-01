#![feature(explicit_tail_calls)]
fn identity<T>(value: T) -> T { value }
fn relay<T>(value: T) -> T {
    let next: fn(T) -> T = identity::<T>;
    become next(value)
}
fn main() {
    assert_eq!(relay(37u16), 37);
    assert_eq!(relay([41u8, 43]), [41, 43]);
}
