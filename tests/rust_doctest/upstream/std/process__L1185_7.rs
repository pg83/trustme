// Extracted from library/std/src/process.rs:1185
#![allow(unused)]
fn main() {
    use std::path::Path;
    use std::process::Command;

    let mut cmd = Command::new("ls");
    assert_eq!(cmd.get_current_dir(), None);
    cmd.current_dir("/bin");
    assert_eq!(cmd.get_current_dir(), Some(Path::new("/bin")));
}
