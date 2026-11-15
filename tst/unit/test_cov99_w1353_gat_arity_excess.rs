//@ compile-only
//@ compile-flags: -Z stop-after=typeck
//@ compile-fail: error:

trait Arity {
    type Item<T>;
}

fn check<T: Arity>(value: <T as Arity>::Item<u8>) {
    let _: <_ as Arity>::Item<u8, u16> = value;
}

fn main() {}
