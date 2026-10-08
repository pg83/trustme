// A trait bound may be written in parentheses: rustc's `parse_generic_bound`
// takes a `(` before a trait bound and the matching `)` after it, in a
// where-clause as in a parameter's bounds. wasm-bindgen's closure conversions
// are bounded `T: 'static + (FnOnce $FnArgs -> R)`.
#![allow(unused_parens)]

fn call<T>(f: T) -> u8
where
    T: 'static + (FnOnce(u8) -> u8),
{
    f(2)
}

fn twice<T: (Fn(u8) -> u8) + Copy>(f: T) -> u8 {
    f(f(1))
}

fn main() {
    assert_eq!(call(|x| x + 1), 3);
    assert_eq!(twice(|x| x * 3), 9);
}
