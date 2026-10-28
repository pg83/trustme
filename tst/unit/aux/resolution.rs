pub mod outer {
    pub struct Item(pub i32);
    pub trait Trait {
        fn value(&self) -> i32;
    }
    impl Trait for Item {
        fn value(&self) -> i32 {
            self.0
        }
    }
    pub mod inner {
        pub const CONSTANT: i32 = 7;
        pub type Alias = super::Item;
    }
}

pub use outer::inner as exported;
