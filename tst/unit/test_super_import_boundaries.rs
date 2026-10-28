const ROOT: u8 = 17;

mod inner {
    use super::ROOT;
    const LOCAL: u8 = 29;

    pub fn from_block() -> u8 {
        use super::ROOT as VALUE;
        VALUE
    }

    pub mod nested {
        use super::super::ROOT;
        use super::LOCAL;

        pub fn sum() -> u8 {
            use super::super::ROOT as FROM_BLOCK;
            ROOT + LOCAL + FROM_BLOCK
        }
    }

    pub fn from_module() -> u8 { ROOT }
}

fn main() {
    assert_eq!(inner::from_module(), 17);
    assert_eq!(inner::from_block(), 17);
    assert_eq!(inner::nested::sum(), 63);
}
