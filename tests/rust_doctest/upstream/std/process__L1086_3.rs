// Extracted from library/std/src/process.rs:1086
#![allow(unused)]
fn main() {
    use std::process::Command;
    
    let status = Command::new("/bin/cat")
        .arg("file.txt")
        .status()
        .expect("failed to execute process");
    
    println!("process finished with: {status}");
    
    assert!(status.success());
}
