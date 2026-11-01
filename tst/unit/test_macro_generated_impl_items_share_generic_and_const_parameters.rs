trait Rows {
    type Row<const N: usize>;
    const BIAS: u16;
    fn row<const N: usize>(&self) -> Self::Row<N>;
}

struct Seed(u16);

macro_rules! rows {
    () => {
        type Row<const N: usize> = [u16; N];
        const BIAS: u16 = 7;
        fn row<const N: usize>(&self) -> Self::Row<N> {
            [self.0 + Self::BIAS; N]
        }
    };
}

impl Rows for Seed {
    rows!();
    #[cfg(any())]
    fn disabled(&self) -> MissingType { loop {} }
}

fn main() {
    assert_eq!(Seed(11).row::<3>(), [18, 18, 18]);
    assert_eq!(Seed(2).row::<0>(), []);
}
