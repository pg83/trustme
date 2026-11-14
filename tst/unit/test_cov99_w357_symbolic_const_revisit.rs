#![feature(generic_const_exprs, generic_const_items)]
#![allow(incomplete_features)]

struct Size<const N: usize>;

const fn measure<T>(_: &T) -> Size<{ core::mem::size_of::<T>() }> {
    Size
}

const AA: usize = {
    let captured = 7u32;
    let closure = || captured;
    let _ = measure(&closure);
    1
} where (): Copy;

struct Check;

impl Check {
    fn check<const N: usize>(&self) {
        assert_eq!(N, 1);
    }
}

fn main() {
    Check.check::<{ match 1 { AA => 1, _ => 0 } }>();
}
