//@ crate-type: lib
//@ env: TRUSTME_CAPS_ORACLE=1
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

pub enum Width {
    Reference = z_width::<u8>() as isize,
}

const fn z_width<T>() -> usize
where
    [(); core::mem::size_of::<&T>()]:,
{
    let bytes = [0u8; core::mem::size_of::<&T>()];
    bytes.len()
}
