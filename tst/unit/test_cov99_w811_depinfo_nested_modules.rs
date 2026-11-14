//@ crate-type: lib
//@ compile-flags: --emit=metadata,dep-info=$OUT.d
//@ output-contains: .d test_cov99_w811_depinfo_nested_modules.rs

pub mod outer {
    pub mod inner {
        pub const VALUE: u32 = 11;
    }
}

pub fn value() -> u32 {
    outer::inner::VALUE
}
