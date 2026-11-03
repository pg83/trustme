//@ aux-build: exported_union_payload.rs
extern crate exported_union_payload as wire;
const FIXED: wire::Payload<u16, 3> = wire::array([13, 17, 19]);
fn main() {
    assert_eq!(unsafe { FIXED.array }, [13, 17, 19]);
    let mut value = wire::array([23u32, 29, 31, 37]);
    assert_eq!(unsafe { value.first }, 23);
    wire::replace(&mut value, [41, 43, 47, 53]);
    assert_eq!(unsafe { value.array }, [41, 43, 47, 53]);
    assert_eq!(unsafe { wire::first(59u64).first }, 59);
}
