// Extracted from library/core/src/unit.rs:7
#![allow(unused)]
fn main() {
    use std::io::*;
    let data = vec![1, 2, 3, 4, 5];
    let res: Result<()> = data.iter()
        .map(|x| writeln!(stdout(), "{x}"))
        .collect();
    assert!(res.is_ok());
}
