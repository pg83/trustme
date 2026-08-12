macro_rules! value {
    (1) => {
        1
    };
}

mod inner {
    pub fn before_shadow() -> i32 {
        value!(1)
    }

    macro_rules! value {
        (2) => {
            2
        };
    }

    pub fn after_shadow() -> i32 {
        value!(2)
    }
}

fn main() {
    assert_eq!(inner::before_shadow(), 1);
    assert_eq!(inner::after_shadow(), 2);
}
