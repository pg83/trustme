
#![feature(lang_items)]

pub enum Ordering {
    /// An ordering where a compared value is less than another.
    Less = -1,
    /// An ordering where a compared value is equal to another.
    Equal = 0,
    /// An ordering where a compared value is greater than another.
    Greater = 1,
}

pub fn f<F: FnOnce(i32) -> Ordering>(g: F) -> Ordering {
    g(1)
}
