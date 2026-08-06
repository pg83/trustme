// Extracted from src/attributes/diagnostics.md:127
#![allow(unused)]
fn main() {
    use std::path::PathBuf;
    
    pub fn get_path() -> PathBuf {
        // The `reason` parameter on `allow` attributes acts as documentation for the reader.
        #[allow(unused_mut, reason = "this is only modified on some platforms")]
        let mut file_name = PathBuf::from("git");
    
        #[cfg(target_os = "windows")]
        file_name.set_extension("exe");
    
        file_name
    }
}
