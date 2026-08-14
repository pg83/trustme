#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

fn make_array<const M: usize>() -> [(); M + 1] {
    [(); M + 1]
}

fn twice_plus_one<const N: usize>() -> [(); (N * 2) + 1] {
    make_array::<{ N * 2 }>()
}

fn main() {
    assert_eq!(twice_plus_one::<4>(), [(); 9]);
}
