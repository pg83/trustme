macro_rules! take_type {
    ($ty:ty) => {};
}

take_type!(for<T = &i32> fn());

fn main() {}
