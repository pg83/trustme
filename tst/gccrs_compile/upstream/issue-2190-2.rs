use std::ops::Deref;

fn foo<T: Deref<Target = i32>>(value: &T) -> i32 {
    (**value).max(2)
}
