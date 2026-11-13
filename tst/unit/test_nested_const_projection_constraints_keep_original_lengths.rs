struct Slot<const N: usize>([u16; N]);

impl<const N: usize> Slot<N> {
    fn filled(value: u16) -> Self {
        Slot([value; N])
    }
}

trait Ready {}
impl<const N: usize> Ready for Slot<N> {}

trait Make<const N: usize> {
    type Output;
}

struct Factory;

impl<const N: usize> Make<N> for Factory
where
    Slot<N>: Ready,
{
    type Output = Slot<N>;
}

fn main() {
    let first = <Factory as Make<_>>::Output::filled(733);
    let first: Slot<2> = first;
    assert_eq!(first.0, [733, 733]);

    let second = <Factory as Make<_>>::Output::filled(739);
    let second: Slot<3> = second;
    assert_eq!(second.0, [739, 739, 739]);
}
