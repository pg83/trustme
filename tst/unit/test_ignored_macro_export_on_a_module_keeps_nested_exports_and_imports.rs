pub fn source_value() -> u16 {
    7
}

#[macro_export]
mod local {
    #[macro_export]
    use crate::source_value as alias;

    #[macro_export]
    macro_rules! root_value {
        () => {
            13u16
        };
    }

    pub fn call_alias() -> u16 {
        alias()
    }
}

fn main() {
    assert_eq!(local::call_alias() + root_value!(), 20);
}
