use super::{alias, second_alias};

pub fn value() -> i32 {
    alias!() + second_alias!()
}
