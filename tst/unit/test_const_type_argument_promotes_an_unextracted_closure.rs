#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

struct Marker<const N: usize>;

struct Holder<const N: usize>
where
    Marker<{
        let _operation = &|| {
            let local = 7u8;
            local + 1
        };
        N
    }>: Sized,
{
    marker: Marker<N>,
}

fn main() {
    let _holder = Holder::<3> { marker: Marker };
}
