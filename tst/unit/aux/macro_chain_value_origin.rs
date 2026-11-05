mod definitions {
    #[macro_export]
    macro_rules! source_value { () => { 37u8 }; }
}
pub use crate::source_value as selected_value;
