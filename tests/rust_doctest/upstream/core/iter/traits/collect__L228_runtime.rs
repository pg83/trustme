// Extracted from library/core/src/iter/traits/collect.rs:228
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        fn collect_as_strings<T>(collection: T) -> Vec<String>
        where
            T: IntoIterator,
            T::Item: std::fmt::Debug,
        {
            collection
                .into_iter()
                .map(|item| format!("{item:?}"))
                .collect()
        }
        Ok(())
    }
    doctest().unwrap();
}
