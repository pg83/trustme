#[link(name = "roundtrip_native_archive", kind = "static")]
extern "C" { fn archive_triple(value: u32) -> u32; }
pub fn compute(value: u32) -> u32 { unsafe { archive_triple(value) + 2 } }
