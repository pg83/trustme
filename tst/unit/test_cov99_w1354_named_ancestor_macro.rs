mod named {
    mod values {
        macro_rules! base {
            () => { 37_u32 };
        }
        pub(super) use base;
    }

    pub fn run() -> u32 {
        const OFFSET: u32 = 5;
        values::base!() + OFFSET
    }
}

fn main() {
    assert_eq!(named::run(), 42);
}
