#![feature(generic_arg_infer)]

type Coupled<const N: usize> = ([u8; N], [u16; N]);

fn relay<const N: usize>(value: Coupled<N>) -> Coupled<N> {
    value
}

fn width<const N: usize>(_: &Coupled<N>) -> usize {
    N
}

fn main() {
    let first: Coupled<_> = ([71, 73], [443, 449]);
    let second: Coupled<_> = ([79, 83, 89], [457, 461, 463]);
    let first = relay(first);
    let second = relay(second);
    assert_eq!(width(&first), 2);
    assert_eq!(width(&second), 3);
    assert_eq!(first.0[1], 73);
    assert_eq!(first.1[1], 449);
    assert_eq!(second.0[2], 89);
    assert_eq!(second.1[2], 463);
}
