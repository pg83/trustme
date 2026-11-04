//@ compile-variant: -O
const VALUE: u32 = 83;

macro_rules! import_value {
    ($path:path) => {
        use $path as selected;
        pub fn read() -> u32 { selected }
    };
}

mod outer {
    pub mod inner {
        import_value!(self::super::super::VALUE);
    }
}

fn main() { assert_eq!(outer::inner::read(), 83); }
