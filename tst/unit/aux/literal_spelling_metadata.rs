#[macro_export]
macro_rules! spelling_values {
    ($receiver:ident) => {
        $receiver!('λ', '\'', '\\', '\0', 1.25f32, 2.5f64, c"branch")
    };
}
