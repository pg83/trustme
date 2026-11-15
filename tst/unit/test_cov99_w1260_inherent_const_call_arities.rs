#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

struct One;
struct Two;

impl One {
    const fn width<const N: usize>() -> usize {
        N
    }
}

impl Two {
    const fn width<const N: usize, const M: usize>() -> usize {
        N + M
    }
}

fn retain<T: Copy>(value: T) -> T {
    value
}

fn choose<const N: usize>(value: [u8; One::width::<N>()]) -> [u8; One::width::<N>()]
where
    [(); Two::width::<N, 1>()]: Sized,
    [(); One::width::<N>()]: Sized,
{
    retain::<[u8; One::width::<N>()]>(value)
}

fn main() {
    assert_eq!(choose::<3>([7; 3]), [7; 3]);
}
