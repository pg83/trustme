// The other side of `dead_code`: a private function any code reaches - by a
// call, as a function pointer, from a closure, through a `use`, from a const
// initializer - is live, and `#![deny(dead_code)]` accepts the crate.
#![deny(dead_code)]

mod inner {
    fn helper() -> u32 {
        1
    }

    pub(crate) fn exposed() -> u32 {
        helper() + 1
    }

    fn via_pointer() -> u32 {
        5
    }

    pub(crate) static POINTER: fn() -> u32 = via_pointer;

    fn via_closure() -> u32 {
        7
    }

    pub(crate) fn closure_user() -> u32 {
        (|| via_closure())()
    }

    fn reexported() -> u32 {
        11
    }

    use self::reexported as renamed;

    pub(crate) fn via_use() -> u32 {
        renamed()
    }
}

fn generic<T: Default>() -> T {
    T::default()
}

const fn in_const() -> usize {
    3
}

const LEN: usize = in_const();

#[allow(dead_code)]
fn allowed() {}

fn _underscored() {}

fn main() {
    assert_eq!(inner::exposed(), 2);
    assert_eq!((inner::POINTER)(), 5);
    assert_eq!(inner::closure_user(), 7);
    assert_eq!(inner::via_use(), 11);
    assert_eq!(generic::<u8>(), 0);
    assert_eq!([0u8; LEN].len(), 3);
}
