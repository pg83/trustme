// Extracted from library/core/src/result.rs:749
#![allow(unused)]
fn main() {
    fn mutate(r: &mut Result<i32, i32>) {
        match r.as_mut() {
            Ok(v) => *v = 42,
            Err(e) => *e = 0,
        }
    }
    
    let mut x: Result<i32, i32> = Ok(2);
    mutate(&mut x);
    assert_eq!(x.unwrap(), 42);
    
    let mut x: Result<i32, i32> = Err(13);
    mutate(&mut x);
    assert_eq!(x.unwrap_err(), 0);
}
