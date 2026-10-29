use std::sync::atomic::{AtomicI32, AtomicU16, AtomicU64, Ordering};
fn main() {
    let signed = AtomicI32::new(-9);
    assert_eq!(signed.fetch_max(-3, Ordering::Acquire), -9);
    assert_eq!(signed.fetch_max(-7, Ordering::Relaxed), -3);
    assert_eq!(signed.fetch_min(-11, Ordering::Release), -3);
    assert_eq!(signed.load(Ordering::SeqCst), -11);

    let unsigned = AtomicU64::new(1 << 63);
    assert_eq!(unsigned.fetch_max(7, Ordering::AcqRel), 1 << 63);
    assert_eq!(unsigned.fetch_min(13, Ordering::SeqCst), 1 << 63);
    assert_eq!(unsigned.load(Ordering::Relaxed), 13);

    let bits = AtomicU16::new(0x0ff0);
    assert_eq!(bits.fetch_nand(0x00ff, Ordering::SeqCst), 0x0ff0);
    assert_eq!(bits.load(Ordering::Relaxed), 0xff0f);
}
