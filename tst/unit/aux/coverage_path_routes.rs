pub mod u16 {
    pub struct Tag { pub value: u16 }
    pub struct Empty {}
    pub const LIMIT: u16 = 43;
    pub mod nested {
        pub type Word = u16;
        pub const VALUE: Word = 317;
    }
}

pub mod api {
    pub trait Schema {
        type Item;
        fn number() -> u16;
    }
    pub struct Runner;
    impl Schema for Runner {
        type Item = u16;
        fn number() -> u16 { 419 }
    }
    pub enum Event { Stop, Data(u16), Record { value: u16 } }
    pub use Schema as AliasSchema;
    pub use Event as AliasEvent;
}
