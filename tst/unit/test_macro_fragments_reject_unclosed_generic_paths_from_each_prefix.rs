//@ compile-fail: error:

macro_rules! reject_paths {
    ($whole:ty) => {};
    (Head < $after_head:ty) => {};
    (Head < crate::Middle < $after_middle:ty) => {};
    (Head < crate::Middle < base:: < $qualified:ty) => {};
}

fn main() {
    reject_paths!(Head < crate::Middle < base:: < < W);
}
