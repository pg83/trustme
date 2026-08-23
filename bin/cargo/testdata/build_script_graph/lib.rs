#[cfg(not(from_build_script))]
compile_error!("build script cfg was not propagated");

include!(concat!(env!("OUT_DIR"), "/generated.rs"));

pub fn generated() -> usize {
    GENERATED
}
