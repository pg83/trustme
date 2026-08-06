// Extracted from library/core/src/result.rs:1316
#![allow(unused)]
#![feature(never_type)]
#![feature(unwrap_infallible)]
fn main() {
    
    fn only_bad_news() -> Result<!, String> {
        Err("Oops, it failed".into())
    }
    
    let error: String = only_bad_news().into_err();
    println!("{error}");
}
