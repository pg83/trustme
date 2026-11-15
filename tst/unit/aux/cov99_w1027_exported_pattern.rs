#[macro_export]
macro_rules! exported_pattern {
    () => {
        11u32
    };
    ($value:expr) => {
        $value + 7u32
    };
}
