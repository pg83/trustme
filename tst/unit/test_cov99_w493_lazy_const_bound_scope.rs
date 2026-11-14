//@ compile-fail: Failed to find impl with 'N'
//@ compile-flags: -Cpanic=abort

mod hidden {
    pub trait Count {
        const N: usize;
    }
    pub trait Family {
        type Item: Count;
    }
    impl Count for () {
        const N: usize = 1;
    }
    impl Family for () {
        type Item = ();
    }
}

const fn z_count<T: hidden::Family>() -> usize {
    T::Item::N
}

fn a_trigger() {
    let _: [u8; z_count::<()>()];
}

fn main() {}
