#![feature(generic_const_exprs, inherent_associated_types)]
#![allow(incomplete_features)]

struct Source<const OUTER: bool>;
struct Target<const VALUE: bool>;

impl<const OUTER: bool> Source<OUTER> {
    type Item<const INNER: bool> = Target<{ OUTER & INNER }>
    where
        [(); { OUTER & INNER } as usize]:;
}

fn main() {
    let _: Source<true>::Item<false> = Target::<false>;
}
