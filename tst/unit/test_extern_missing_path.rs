// A path that names no file is only an error if the crate is used: `--extern
// Name=/nowhere` on a crate that never mentions `Name` is accepted.
//@ compile-flags: --extern LooksLikeExternCrate=/path/to/nowhere

mod m {
    pub struct LooksLikeExternCrate;
}

fn main() {
    let _s = m::LooksLikeExternCrate {};
}
