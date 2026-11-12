//@ compile-fail: Unexpected token

macro_rules! require_trait {
    ($bound:ty) => {
        fn constrained<T: $bound>() {}
    };
}

require_trait!((u8, u16));

fn main() {}
