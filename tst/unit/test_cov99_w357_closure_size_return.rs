#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

struct Size<const N: usize>;

fn measure<T>(_: T) -> Size<{ core::mem::size_of::<T>() }> {
    Size
}

fn main() {
    let captured = 7u32;
    let _ = measure(|| captured);
}
