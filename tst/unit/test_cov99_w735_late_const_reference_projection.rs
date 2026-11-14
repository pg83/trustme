trait Select {
    type Output;
    fn output() -> Self::Output;
}

struct Choice<const N: usize>;

impl Select for Choice<1> {
    type Output = &'static u8;
    fn output() -> Self::Output {
        &83
    }
}

impl Select for Choice<2> {
    type Output = *const u8;
    fn output() -> Self::Output {
        &97
    }
}

fn select<const N: usize>(_: [u8; N]) -> <Choice<N> as Select>::Output
where
    Choice<N>: Select,
{
    Choice::<N>::output()
}

struct Seed;

impl Seed {
    fn array(self) -> [u8; 1] {
        [0]
    }
}

fn main() {
    let value = *select(Seed.array());
    assert_eq!(value, 83);
}
