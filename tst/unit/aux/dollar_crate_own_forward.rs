use dollar_crate_probe::{echo, first_token};

pub const VALUE: u32 = 11;

macro_rules! forward {
    ($($m:tt)*) => {
        $($m)*!($crate::VALUE)
    };
}

pub fn first() -> &'static str {
    forward!(first_token)
}

pub fn value() -> u32 {
    forward!(echo)
}
