//@ env: TRUSTME_CAPS_ORACLE=1
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

fn pointer_chunk<T>(bytes: &[u8]) -> usize
where
    [(); core::mem::size_of::<*const T>()]:,
{
    let chunk = bytes.first_chunk::<{ core::mem::size_of::<*const T>() }>().unwrap();
    assert_eq!(chunk[0], 37);
    chunk.len()
}

fn main() {
    let bytes = [37; core::mem::size_of::<usize>()];
    assert_eq!(pointer_chunk::<u16>(&bytes), bytes.len());
}
