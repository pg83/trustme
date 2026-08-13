macro_rules! accept_item {
    ($item:item) => {};
}

accept_item! {
    extern crate self;
}

fn main() {}
