//@ compile-fail: error:

trait Choice<T: ?Sized> {}

struct Witness;

impl Choice<[u8]> for Witness {}

fn source<T>() -> &'static T
where
    Witness: Choice<T>,
{
    loop {}
}

fn main() {
    let _: &[u8] = source();
}
