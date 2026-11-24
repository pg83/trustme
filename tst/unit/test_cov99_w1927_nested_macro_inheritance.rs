macro_rules! root_part {
    (value) => { 13u32 };
}

mod branch {
    macro_rules! branch_part {
        (value) => { 29u32 };
    }

    mod leaf {
        pub fn total() -> u32 {
            root_part!(value) + branch_part!(value)
        }
    }

    pub fn total() -> u32 {
        leaf::total()
    }
}

fn main() {
    assert_eq!(branch::total(), 42u32);
}
