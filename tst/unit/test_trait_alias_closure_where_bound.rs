#![feature(trait_alias)]

trait Split<F> = Fn(i32) -> i32 where F: Fn(u32) -> u32;

fn apply<T: Split<F>, F>(first: T, second: F) -> (i32, u32) {
    (first(1), second(2))
}

fn main() {
    assert_eq!(apply(|value| value + 1, |value| value + 1), (2, 3));
}
