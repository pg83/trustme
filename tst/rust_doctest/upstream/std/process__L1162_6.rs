// Extracted from library/std/src/process.rs:1162
#![allow(unused)]
fn main() {
    use std::ffi::OsStr;
    use std::process::Command;

    let mut cmd = Command::new("ls");
    cmd.env("TERM", "dumb").env_remove("TZ");
    let envs: Vec<(&OsStr, Option<&OsStr>)> = cmd.get_envs().collect();
    assert_eq!(envs, &[
        (OsStr::new("TERM"), Some(OsStr::new("dumb"))),
        (OsStr::new("TZ"), None)
    ]);
}
