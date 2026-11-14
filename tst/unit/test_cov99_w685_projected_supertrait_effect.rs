//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

trait Input {
    type Arg;
}

trait Base<T> {
    type Out;
}

trait Child<I: Input>: Base<I::Arg> {}

trait Host<I: Input> {
    type Value: Child<I, Out = ()>;
}

impl<S: ?Sized, T> Base<T> for S {
    type Out = ();
}

fn require_base<I: Input, S: ?Sized + Base<I::Arg>>(_: &S) {}

fn check<I: Input, H: Host<I>>(value: &H::Value) {
    require_base::<I, _>(value);
}

fn main() {}
