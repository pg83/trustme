// Extracted from library/core/src/pin.rs:68
#![allow(unused)]
fn main() {
    #[derive(Default)]
    struct AddrTracker(Option<usize>);
    
    impl AddrTracker {
        // If we haven't checked the addr of self yet, store the current
        // address. If we have, confirm that the current address is the same
        // as it was last time, or else panic.
        fn check_for_move(&mut self) {
            let current_addr = self as *mut Self as usize;
            match self.0 {
                None => self.0 = Some(current_addr),
                Some(prev_addr) => assert_eq!(prev_addr, current_addr),
            }
        }
    }
    
    // Create a tracker and store the initial address
    let mut tracker = AddrTracker::default();
    tracker.check_for_move();
    
    // Here we shadow the variable. This carries a semantic move, and may therefore also
    // come with a mechanical memory *move*
    let mut tracker = tracker;
    
    // May panic!
    // tracker.check_for_move();
}
