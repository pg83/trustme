//@ crate-type: lib
//@ compile-fail: Failed to find an impl of

trait Base<T> {
    type Out;
}

trait Both: Base<u8, Out = u32> + Base<u16, Out = u64> {}

fn require<T: ?Sized + Base<u16, Out = u32>>(_: &T) {}

fn probe(value: &dyn Both<Out = u32>) {
    require(value);
}
