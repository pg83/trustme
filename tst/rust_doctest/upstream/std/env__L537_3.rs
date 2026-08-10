// Extracted from library/std/src/env.rs:537
use std::env;
use std::ffi::OsString;
use std::path::Path;

fn main() -> Result<(), env::JoinPathsError> {
if cfg!(unix) {
    let paths = [Path::new("/bin"), Path::new("/usr/bin")];
    let path_os_string = env::join_paths(paths.iter())?;
    assert_eq!(path_os_string, OsString::from("/bin:/usr/bin"));
}
    Ok(())
}
