// Extracted from library/std/src/os/windows/process.rs:315
#![allow(unused)]
#![feature(windows_process_extensions_raw_attribute)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::os::windows::io::AsRawHandle;
        use std::os::windows::process::{CommandExt, ProcThreadAttributeList};
        use std::process::Command;
        
        struct ProcessDropGuard(std::process::Child);
        impl Drop for ProcessDropGuard {
            fn drop(&mut self) {
                let _ = self.0.kill();
            }
        }
        
        let parent = Command::new("cmd").spawn()?;
        let parent_process_handle = parent.as_raw_handle();
        let parent = ProcessDropGuard(parent);
        
        const PROC_THREAD_ATTRIBUTE_PARENT_PROCESS: usize = 0x00020000;
        let mut attribute_list = ProcThreadAttributeList::build()
            .attribute(PROC_THREAD_ATTRIBUTE_PARENT_PROCESS, &parent_process_handle)
            .finish()
            .unwrap();
        
        let mut child = Command::new("cmd").spawn_with_attributes(&attribute_list)?;
        
        child.kill()?;
        Ok::<(), std::io::Error>(())
    }
    doctest().unwrap();
}
