#![feature(generic_const_exprs)]

struct Step<const BASE: usize, const WIDTH: usize>;
impl<const N: usize> Step<N, { N + 1 }> where [(); N + 1]: {
    fn next(&self) -> usize { N + 1 }
}
impl<const N: usize> Step<N, { N + 2 }> where [(); N + 2]: {
    fn skip(&self) -> usize { N + 2 }
}

fn widths<const N: usize>() -> (usize, usize)
where [(); N + 1]:, [(); N + 2]: {
    let next = Step::<N, { N + 1 }>;
    let skip = Step::<N, { N + 2 }>;
    (next.next(), skip.skip())
}

fn main() {
    assert_eq!(widths::<3>(), (4, 5));
    assert_eq!(widths::<7>(), (8, 9));
}
