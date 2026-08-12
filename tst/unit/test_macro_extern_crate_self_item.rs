macro_rules! take_item {
    ($item:item) => {};
}

take_item! {
    extern crate self;
}

fn main() {}
