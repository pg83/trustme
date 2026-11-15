//@ compile-only

#![allow(dead_code)]

const fn width<const N: usize>(_: [(); N]) -> usize {
    N
}

trait Value<const N: usize> {}
trait Both: Value<{ width([]) }> + Value<{ width::<0>([]) }> {}

fn main() {}
