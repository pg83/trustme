struct Packet<const N: usize>([u16; N]);

impl<const N: usize> Packet<N> {
    fn filled(value: u16) -> Self {
        Packet([value; N])
    }

    fn width(&self) -> usize {
        N
    }
}

trait Make<const N: usize> {
    type Output;
}

struct Maker;

impl<const N: usize> Make<N> for Maker {
    type Output = Packet<N>;
}

fn main() {
    let short = <Maker as Make<_>>::Output::filled(719);
    let short: Packet<2> = short;
    assert_eq!(short.width(), 2);
    assert_eq!(short.0, [719, 719]);

    let long = <Maker as Make<_>>::Output::filled(727);
    let long: Packet<3> = long;
    assert_eq!(long.width(), 3);
    assert_eq!(long.0, [727, 727, 727]);
}
