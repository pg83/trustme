// Repeated item captures must retain cfg attributes while cloning the module.
macro_rules! copy_module {
    ($item:item) => {
        mod first { $item }
        mod second { $item }
    };
}

copy_module! {
    pub mod payload {
        #[cfg(any())]
        mod missing_file;

        #[cfg(any())]
        mod disabled_inline {
            pub const VALUE: u32 = 999;
        }

        #[cfg(any())]
        fn disabled_function() -> u32 { 999 }

        pub const VALUE: u32 = 42;
    }
}

fn main() {
    assert_eq!(first::payload::VALUE, 42);
    assert_eq!(second::payload::VALUE, 42);
}
