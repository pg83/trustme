// Extracted from library/std/src/os/windows/process.rs:558
#![allow(unused)]
#![feature(windows_process_extensions_raw_attribute)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::ffi::c_void;
        use std::os::windows::process::{CommandExt, ProcThreadAttributeList};
        use std::os::windows::raw::HANDLE;
        use std::process::Command;
        
        #[repr(C)]
        pub struct COORD {
            pub X: i16,
            pub Y: i16,
        }
        
        unsafe extern "system" {
            fn CreatePipe(
                hreadpipe: *mut HANDLE,
                hwritepipe: *mut HANDLE,
                lppipeattributes: *const c_void,
                nsize: u32,
            ) -> i32;
            fn CreatePseudoConsole(
                size: COORD,
                hinput: HANDLE,
                houtput: HANDLE,
                dwflags: u32,
                phpc: *mut isize,
            ) -> i32;
            fn CloseHandle(hobject: HANDLE) -> i32;
        }
        
        let [mut input_read_side, mut output_write_side, mut output_read_side, mut input_write_side] =
            [unsafe { std::mem::zeroed::<HANDLE>() }; 4];
        
        unsafe {
            CreatePipe(&mut input_read_side, &mut input_write_side, std::ptr::null(), 0);
            CreatePipe(&mut output_read_side, &mut output_write_side, std::ptr::null(), 0);
        }
        
        let size = COORD { X: 60, Y: 40 };
        let mut h_pc = unsafe { std::mem::zeroed() };
        unsafe { CreatePseudoConsole(size, input_read_side, output_write_side, 0, &mut h_pc) };
        
        unsafe { CloseHandle(input_read_side) };
        unsafe { CloseHandle(output_write_side) };
        
        const PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE: usize = 131094;
        
        let attribute_list = unsafe {
            ProcThreadAttributeList::build()
                .raw_attribute(
                    PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                    h_pc as *const c_void,
                    size_of::<isize>(),
                )
                .finish()?
        };
        
        let mut child = Command::new("cmd").spawn_with_attributes(&attribute_list)?;
        
        child.kill()?;
        Ok::<(), std::io::Error>(())
    }
    doctest().unwrap();
}
