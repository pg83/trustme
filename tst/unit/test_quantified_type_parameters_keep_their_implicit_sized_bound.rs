#![feature(non_lifetime_binders)]
#![allow(incomplete_features)]

trait Stamp<T> {
    fn stamp() -> u16;
}

struct Bridge;

impl<T> Stamp<T> for Bridge {
    fn stamp() -> u16 {
        97
    }
}

fn use_every_type<M>() -> u16
where
    M: for<T> Stamp<T>,
{
    <M as Stamp<u8>>::stamp() + <M as Stamp<[u16; 2]>>::stamp()
}

fn main() {
    assert_eq!(use_every_type::<Bridge>(), 194);
}
