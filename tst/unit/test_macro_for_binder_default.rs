macro_rules! accept_type {
    ($type:ty) => {};
}

accept_type!(for<T = &i32> fn());

fn main() {}
