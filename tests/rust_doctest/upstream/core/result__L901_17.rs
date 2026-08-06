// Extracted from library/core/src/result.rs:901
#![allow(unused)]
fn main() {
    fn stringify(x: u32) -> String { format!("error code: {x}") }
    
    let x: Result<u32, u32> = Ok(2);
    assert_eq!(x.map_err(stringify), Ok(2));
    
    let x: Result<u32, u32> = Err(13);
    assert_eq!(x.map_err(stringify), Err("error code: 13".to_string()));
}
