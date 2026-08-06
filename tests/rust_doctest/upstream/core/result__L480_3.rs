// Extracted from library/core/src/result.rs:480
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::str::FromStr;
        let mut results = vec![];
        let mut errs = vec![];
        let nums: Vec<_> = ["17", "not a number", "99", "-27", "768"]
           .into_iter()
           .map(u8::from_str)
           // Save clones of the raw `Result` values to inspect
           .inspect(|x| results.push(x.clone()))
           // Challenge: explain how this captures only the `Err` values
           .inspect(|x| errs.extend(x.clone().err()))
           .flatten()
           .collect();
        assert_eq!(errs.len(), 3);
        assert_eq!(nums, [17, 99]);
        println!("results {results:?}");
        println!("errs {errs:?}");
        println!("nums {nums:?}");
        Ok(())
    }
    doctest().unwrap();
}
