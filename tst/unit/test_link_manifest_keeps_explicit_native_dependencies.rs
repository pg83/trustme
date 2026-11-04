//@ compile-flags: -lc -Cemit-link-manifest=$OUT.links -Lnative=$OUT.native -Clink-arg=-Wl,--build-id=none
//@ output-contains: .links search	/
//@ output-contains: .links lib	c
//@ output-contains: .links arg	-Wl,--build-id=none
#[link(name = "c")]
extern "C" { fn getpid() -> i32; }
fn main() { assert_eq!(unsafe { getpid() } as u32, std::process::id()); }
