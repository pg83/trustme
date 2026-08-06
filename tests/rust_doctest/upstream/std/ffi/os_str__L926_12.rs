// Extracted from library/std/src/ffi/os_str.rs:926
#![allow(unused)]
fn main() {
    // Note, due to differences in how Unix and Windows represent strings,
    // we are forced to complicate this example, setting up example `OsStr`s
    // with different source data and via different platform extensions.
    // Understand that in reality you could end up with such example invalid
    // sequences simply through collecting user command line arguments, for
    // example.
    
    #[cfg(unix)] {
        use std::ffi::OsStr;
        use std::os::unix::ffi::OsStrExt;
    
        // Here, the values 0x66 and 0x6f correspond to 'f' and 'o'
        // respectively. The value 0x80 is a lone continuation byte, invalid
        // in a UTF-8 sequence.
        let source = [0x66, 0x6f, 0x80, 0x6f];
        let os_str = OsStr::from_bytes(&source[..]);
    
        assert_eq!(os_str.to_string_lossy(), "fo�o");
    }
    #[cfg(windows)] {
        use std::ffi::OsString;
        use std::os::windows::prelude::*;
    
        // Here the values 0x0066 and 0x006f correspond to 'f' and 'o'
        // respectively. The value 0xD800 is a lone surrogate half, invalid
        // in a UTF-16 sequence.
        let source = [0x0066, 0x006f, 0xD800, 0x006f];
        let os_string = OsString::from_wide(&source[..]);
        let os_str = os_string.as_os_str();
    
        assert_eq!(os_str.to_string_lossy(), "fo�o");
    }
}
