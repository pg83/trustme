#![feature(generic_const_exprs, inherent_associated_types)]
#![allow(incomplete_features)]

struct Source<const OUTER: usize>;
struct Target<const VALUE: usize>;

impl<const OUTER: usize> Source<OUTER> {
    type Item<const INNER: usize> = Target<{ OUTER + INNER }>
    where
        [(); OUTER + INNER]:;
}

fn main() {
    let _: Source<2>::Item<3> = Target::<5>;
}
