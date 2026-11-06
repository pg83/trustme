mod outer {
    pub const TOKEN: u8 = 17;

    pub fn value() -> u8 {
        const TOKEN: u8 = 99;
        mod nested {
            pub mod leaf {
                pub fn read() -> u8 { super::super::TOKEN }
            }
        }
        assert_eq!(TOKEN, 99);
        nested::leaf::read() + self::TOKEN
    }
}

fn main() { assert_eq!(outer::value(), 34); }
