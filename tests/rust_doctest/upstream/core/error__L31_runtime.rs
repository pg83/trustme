// Extracted from library/core/src/error.rs:31
#![allow(unused)]
fn main() {
    use std::error::Error;
    use std::fmt;
    use std::path::PathBuf;
    
    #[derive(Debug)]
    struct ReadConfigError {
        path: PathBuf
    }
    
    impl fmt::Display for ReadConfigError {
        fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
            let path = self.path.display();
            write!(f, "unable to read configuration at {path}")
        }
    }
    
    impl Error for ReadConfigError {}
}
