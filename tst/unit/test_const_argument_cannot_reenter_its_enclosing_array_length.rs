//@ compile-fail: error:0: Loop in constant evaluation

const fn width<const N: usize>() -> usize {
    N
}

const SELF_SIZED: [u8; width::<{ SELF_SIZED.len() }>()] = [];

fn main() {}
