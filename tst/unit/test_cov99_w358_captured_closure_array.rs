#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

const fn bytes<F>(_: &F) -> [u8; core::mem::size_of::<F>()]
where [(); core::mem::size_of::<F>()]:
{
    [17; core::mem::size_of::<F>()]
}

const WIDTH: usize = {
    let word = 7u16;
    let callback = move || word;
    bytes(&callback).len()
};

fn main() {
    assert_eq!(WIDTH, 2);
}
