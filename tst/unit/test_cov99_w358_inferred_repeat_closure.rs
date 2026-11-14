//@ compile-flags: -Z dump-hir
#![feature(generic_const_exprs, generic_arg_infer)]
#![allow(incomplete_features)]

const fn shape<F>(_: &F) -> [u8; core::mem::size_of::<F>()]
where [(); core::mem::size_of::<F>()]:
{
    [0; core::mem::size_of::<F>()]
}

const WIDTH: usize = {
    let word = 13u16;
    let callback = move || word;
    let mut bytes = shape(&callback);
    bytes = [23; _];
    bytes.len()
};

struct Early([u8; WIDTH]);

fn main() {
    let value = Early([5; 2]);
    assert_eq!(WIDTH, 2);
    assert_eq!(value.0, [5, 5]);
}
