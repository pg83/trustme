#![allow(async_fn_in_trait, dead_code)]

trait Identity<T> {
    async fn identity(&self, value: T) -> T;
}

impl<T> Identity<T> for () {
    async fn identity(&self, value: T) -> T {
        value
    }
}

fn main() {}
