//@ compile-fail: type annotations needed
fn zero<T: Default>() -> T { T::default() }
fn main() {
    let (first, .., last) = zero();
    let _: u16 = first;
    let _: u32 = last;
}
