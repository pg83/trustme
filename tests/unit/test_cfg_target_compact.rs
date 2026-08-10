#![feature(cfg_target_compact)]

#[cfg(target(os = "linux", arch = "x86_64", pointer_width = "64"))]
fn selected() -> u32 {
    42
}

#[cfg(not(target(os = "linux", arch = "x86_64", pointer_width = "64")))]
compile_error!("compact target cfg did not match the active target");

fn main() {
    assert_eq!(selected(), 42);
}
