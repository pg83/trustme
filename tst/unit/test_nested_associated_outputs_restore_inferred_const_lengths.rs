struct Buffer<const N: usize>([u16; N]);

impl<const N: usize> Buffer<N> {
    fn filled(value: u16) -> Self {
        Buffer([value; N])
    }
}

trait Echo {
    type Output;
}

impl<const N: usize> Echo for Buffer<N> {
    type Output = Self;
}

trait Make<const N: usize> {
    type Output;
}

struct Factory;

impl<const N: usize> Make<N> for Factory {
    type Output = <Buffer<N> as Echo>::Output;
}

fn main() {
    let short = <Factory as Make<_>>::Output::filled(743);
    let short: Buffer<2> = short;
    assert_eq!(short.0, [743, 743]);

    let long = <Factory as Make<_>>::Output::filled(751);
    let long: Buffer<3> = long;
    assert_eq!(long.0, [751, 751, 751]);
}
