//@ compile-fail: error

mod first {
    #[macro_export]
    macro_rules! shared {
        () => { 17u8 };
    }
}

mod second {
    #[macro_export]
    macro_rules! shared {
        () => { 29u8 };
    }
}

fn main() {}
