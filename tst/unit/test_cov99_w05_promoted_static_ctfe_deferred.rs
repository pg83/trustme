#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

const fn lifted<const N: usize>() -> &'static usize {
    &N
}

const fn deferred<const N: usize>() -> &'static usize
where
    [(); N + 0]:,
{
    lifted::<{ N + 0 }>()
}

const LITERAL: &usize = lifted::<9>();
const EXPRESSION: &usize = deferred::<9>();

fn main() {
    assert_eq!(*LITERAL, 9);
    assert_eq!(*EXPRESSION, 9);
}
