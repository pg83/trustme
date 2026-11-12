#![feature(generic_const_items)]
#![allow(incomplete_features)]

struct Layout;

impl Layout {
    const WIDTH<T>: usize = core::mem::size_of::<T>() where T: Copy;
}

const SENTINEL: u16 = 583;

macro_rules! with_cfg {
    ($statement:stmt) => {
        #[cfg(all())]
        $statement
    };
}

macro_rules! through_path {
    ($scope:path) => {{
        use $scope::{Layout, SENTINEL};
        (SENTINEL, Layout::WIDTH::<u16>)
    }};
}

fn main() {
    with_cfg! { const EXPECTED: usize = 2; }
    assert_eq!(through_path!(self), (583, EXPECTED));
    assert_eq!(Layout::WIDTH::<u32>, 4);
}
