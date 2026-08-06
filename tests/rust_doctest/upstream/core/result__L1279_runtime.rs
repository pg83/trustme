// Extracted from library/core/src/result.rs:1279
#![allow(unused)]
#![feature(never_type)]
#![feature(unwrap_infallible)]
fn main() {
    
    fn only_good_news() -> Result<String, !> {
        Ok("this is fine".into())
    }
    
    let s: String = only_good_news().into_ok();
    println!("{s}");
}
