// A `TypeId` is a hash of the type, not a runtime address: two runs of the same
// binary have to agree on it (rusty-fork passes fork-point ids between
// processes in an environment variable).
use std::any::TypeId;
use std::env;
use std::process::Command;

struct Marker;

fn main() {
    let id = format!("{:?}", TypeId::of::<(Marker, [Option<u32>; 3])>());
    if env::var_os("TRUSTME_TYPEID_CHILD").is_some() {
        print!("{}", id);
        return;
    }
    let exe = env::current_exe().expect("current_exe");
    let out = Command::new(exe)
        .env("TRUSTME_TYPEID_CHILD", "1")
        .output()
        .expect("spawn");
    assert!(out.status.success(), "child failed: {:?}", out.status);
    assert_eq!(
        id,
        String::from_utf8_lossy(&out.stdout),
        "TypeId differs between processes"
    );
}
