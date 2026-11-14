#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

trait Steps<const N: usize, const M: usize = { N + 1 }> {
    const WIDTH: usize = M;
}

impl Steps<3> for () {}

const WIDTH: usize = <() as Steps<_>>::WIDTH;

fn main() {
    assert_eq!(WIDTH, 4);
}
