trait Seeds {
    const SEED: usize;
}

impl<T: ?Sized> Seeds for T {
    const SEED: usize = 3;
}

trait Local {
    const WIDTH: usize = Self::SEED;
}

impl Local for u8 {}

struct Buffer<const N: usize>;

impl Buffer<3> {
    const WIDTH: usize = 3;
}

trait Identity {
    type Output;
}

impl Identity for Buffer<3> {
    type Output = Buffer<3>;
}

type Trigger = [u8; <Buffer<{ <u8 as Local>::WIDTH }> as Identity>::Output::WIDTH];

fn main() {
    let bytes: Trigger = [0; 3];
    assert_eq!(bytes.len(), 3);
}
