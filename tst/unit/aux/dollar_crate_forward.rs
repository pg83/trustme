pub const VALUE: u32 = 7;

#[macro_export]
macro_rules! forward {
    ($($m:tt)*) => {
        $($m)*!($crate::VALUE)
    };
}
