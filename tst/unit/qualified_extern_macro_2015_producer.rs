#[macro_export]
macro_rules! external_macro {
    ($value:expr) => {
        $value + 1usize
    };
}
