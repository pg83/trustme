// Extracted from library/core/src/result.rs:1400
#![allow(unused)]
fn main() {
    use std::{io::ErrorKind, path::Path};

    // Note: on Windows "/" maps to "C:\"
    let root_modified_time = Path::new("/").metadata().and_then(|md| md.modified());
    assert!(root_modified_time.is_ok());

    let should_fail = Path::new("/bad/path").metadata().and_then(|md| md.modified());
    assert!(should_fail.is_err());
    assert_eq!(should_fail.unwrap_err().kind(), ErrorKind::NotFound);
}
