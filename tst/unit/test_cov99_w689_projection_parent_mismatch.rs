//@ compile-fail: Failed to find an impl
//@ compile-flags: --emit=metadata -Cpanic=abort

trait Parent<T> {
    type Out;
}

trait Child: Parent<u8> + Parent<u16> {}

trait Container {
    type Item: Child<Out = ()>;
}

fn consume<T: Parent<u8, Out = bool>>() {}

fn check<C: Container>() {
    consume::<C::Item>();
}

fn main() {}
