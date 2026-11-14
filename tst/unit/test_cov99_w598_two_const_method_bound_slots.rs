trait Shape<const R: usize, const C: usize> {}

impl Shape<3, 5> for () {}

struct Grid;

impl Grid {
    fn dimensions<const R: usize, const C: usize>(&self) -> (usize, usize)
    where
        (): Shape<R, C>,
    {
        (R, C)
    }
}

fn main() {
    assert_eq!(Grid.dimensions(), (3, 5));
}
