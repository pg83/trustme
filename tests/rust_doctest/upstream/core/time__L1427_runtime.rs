// Extracted from library/core/src/time.rs:1427
#![allow(unused)]
fn main() {
    use std::time::Duration;
    
    if let Err(e) = Duration::try_from_secs_f32(-1.0) {
        println!("Failed conversion to Duration: {e}");
    }
}
