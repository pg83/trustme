#[link(name = "coverage_native_math", kind = "static", modifiers = "-bundle")]
extern "C" { fn coverage_native_scale(value: u32) -> u32; }
pub fn scale(value: u32) -> u32 { unsafe { coverage_native_scale(value) } }
