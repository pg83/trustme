pub struct Inner(pub u32);

#[macro_export]
macro_rules! wrapped {
    ($(#[$m:meta])* $name:ident) => {
        $(#[$m])*
        pub struct $name(pub $crate::Inner);
    };
}
