#[link(name = "orchard_native_ops", kind = "static", modifiers = "-bundle")]
extern "C" { fn orchard_update(slot: *mut u32, increment: u32) -> u32; }

pub fn update(value: &mut u32, increment: u32) -> u32 {
    unsafe { orchard_update(value, increment) }
}
