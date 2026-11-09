mod storage {
    pub struct Entry {
        pub value: u32,
    }
}

pub use storage::Entry as PublicEntry;

#[macro_export]
macro_rules! located_entry {
    ($value:expr) => {
        ($crate::PublicEntry { value: $value }, file!(), line!())
    };
}
